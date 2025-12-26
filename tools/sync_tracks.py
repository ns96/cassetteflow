import os
import shutil
import argparse
import time
from pathlib import Path

# ==========================================
# Configuration (Can be overridden by args)
# ==========================================
SOURCE_DIR = r"C:\mp3files" 
DEST_DIR = r"D:/"
# ==========================================

def get_relative_path_key(path_str):
    """Normalize path to use forward slashes for consistent key lookup"""
    return path_str.replace('\\', '/').strip()

def parse_audiodb(db_path):
    """
    Parses audiodb.txt.
    Returns a dict: { relative_source_path -> sd_card_filename }
    """
    mapping = {}
    if not os.path.exists(db_path):
        print(f"Error: audiodb.txt not found at {db_path}")
        return mapping

    print(f"Parsing DB: {db_path}")
    try:
        with open(db_path, 'r', encoding='utf-8') as f:
            for line in f:
                if not line.strip(): continue
                parts = line.strip().split('\t')
                
                # Expecting at least 4 columns.
                # Col 0: Hash
                # Col 1: Length
                # Col 2: Bitrate
                # Col 3: /sdcard/SD_FILENAME
                # Col 4: RELATIVE_SOURCE_PATH (New!)
                
                if len(parts) >= 4:
                    sd_path_full = parts[3] # e.g. /sdcard/song_HASH.mp3
                    sd_filename = sd_path_full.split('/')[-1] # song_HASH.mp3
                    
                    if len(parts) >= 5:
                        rel_path = parts[4]
                        key = get_relative_path_key(rel_path)
                        mapping[key] = sd_filename
                    else:
                        # Fallback for old DB format (or if line is malformed)
                        # Assume filename based mapping if relative path missing?
                        # But for this script to work robustly, we really need the relative path.
                        # We will try to use filename as key if rel path missing, but warn.
                        print(f"Warning: Line missing relative path column: {line.strip()[:50]}...")
                        # If the file is in root, finding it by filename might work.
                        pass
                        
    except Exception as e:
        print(f"Error reading audiodb.txt: {e}")
    
    return mapping

def format_size(size_bytes):
    gb = size_bytes / (1024**3)
    if gb >= 0.01:
        return f"{gb:.2f} GB"
    mb = size_bytes / (1024**2)
    return f"{mb:.2f} MB"

def sync_tracks(source_root, dest_root, dry_run=False, limit=None):
    start_time = time.time()
    
    db_path = os.path.join(source_root, 'audiodb.txt')
    file_map = parse_audiodb(db_path)
    
    print(f"DEBUG: Loaded {len(file_map)} mappings from DB.")
    
    if not file_map:
        print("Aborting: No valid mappings found in audiodb.txt")
        return

    if limit:
        print(f"DEBUG: Limit set to {limit} files")

    files_found = 0
    files_copied = 0
    files_skipped = 0
    
    stats = {
        'mp3': {'count': 0, 'bytes': 0},
        'flac': {'count': 0, 'bytes': 0},
        'other': {'count': 0, 'bytes': 0}
    }
    
    if not dry_run:
        if not os.path.exists(dest_root):
             try:
                os.makedirs(dest_root, exist_ok=True)
             except Exception as e:
                 print(f"Error creating destination directory {dest_root}: {e}")
                 return

    print(f"\nScanning {source_root} for mp3/flac files...")

    # We walk the source directory to find physical files
    for root, dirs, files in os.walk(source_root):
        if limit and files_copied >= limit:
            break
            
        for file in files:
            if limit and files_copied >= limit:
                break
                
            ext = os.path.splitext(file)[1].lower()
            if ext in ['.mp3', '.flac']:
                files_found += 1
                
                src_file_abs = os.path.join(root, file)
                
                # Calculate relative path from source_root
                try:
                    rel_path = os.path.relpath(src_file_abs, source_root)
                    rel_key = get_relative_path_key(rel_path)
                except ValueError:
                    print(f"Skipping file outside source root: {src_file_abs}")
                    continue
                
                # Check if this file is in our DB map
                if rel_key in file_map:
                    sd_filename = file_map[rel_key]
                    dst_file_abs = os.path.join(dest_root, sd_filename)
                    
                    try:
                        fsize = os.path.getsize(src_file_abs)
                    except:
                        fsize = 0

                    if ext == '.mp3':
                        stats['mp3']['count'] += 1
                        stats['mp3']['bytes'] += fsize
                    elif ext == '.flac':
                        stats['flac']['count'] += 1
                        stats['flac']['bytes'] += fsize
                    else:
                        stats['other']['count'] += 1
                        stats['other']['bytes'] += fsize

                    if dry_run:
                        print(f"[DRY RUN] Would copy: {rel_path} -> {sd_filename}")
                        files_copied += 1
                    else:
                        try:
                            # Only copy if size/mtime different? Or always?
                            # For safety/simplicity, logic here overwrites. 
                            # Optimizable later.
                            shutil.copy2(src_file_abs, dst_file_abs)
                            files_copied += 1
                        except Exception as e:
                            print(f"Failed to copy {file}: {e}")
                    
                    if files_copied > 0 and files_copied % 100 == 0:
                        print(f"Progress: {files_copied} files copied...")
                else:
                    # File on disk but not in DB (maybe index outdated?)
                    files_skipped += 1
                    # print(f"Skipping (Not in DB): {rel_path}")

    # Copy audiodb.txt
    if not dry_run and os.path.exists(db_path):
        try:
            shutil.copy2(db_path, os.path.join(dest_root, 'audiodb.txt'))
            print("\nCopied audiodb.txt to destination.")
        except Exception as e:
            print(f"\nFailed to copy audiodb.txt: {e}")
    elif dry_run and os.path.exists(db_path):
         print("\n[DRY RUN] Would copy audiodb.txt to destination.")

    end_time = time.time()
    duration = end_time - start_time
    total_bytes = stats['mp3']['bytes'] + stats['flac']['bytes'] + stats['other']['bytes']

    print("\n" + "="*40)
    print(f"Sync Complete {'(DRY RUN)' if dry_run else ''}")
    print("="*40)
    print(f"Source:             {source_root}")
    print(f"Destination:        {dest_root}")
    if limit:
        print(f"Limit:              {limit} files")
    print(f"Files in DB:        {len(file_map)}")
    print(f"Files Scanned:      {files_found}")
    print(f"Files Skipped:      {files_skipped} (Not in DB)")
    print("-" * 40)
    print("Files Copied Details:")
    print(f"  MP3:  {stats['mp3']['count']:>5} files | {format_size(stats['mp3']['bytes'])}")
    print(f"  FLAC: {stats['flac']['count']:>5} files | {format_size(stats['flac']['bytes'])}")
    print("-" * 40)
    print(f"TOTAL:  {files_copied:>5} files | {format_size(total_bytes)}")
    print(f"Time Elapsed:       {duration:.2f} seconds")
    print("="*40)

def main():
    parser = argparse.ArgumentParser(description="Sync MP3/FLAC files to SD card using relative paths from audiodb.txt")
    parser.add_argument("--dry-run", action="store_true", help="Scan without copying")
    parser.add_argument("--limit", type=int, help="Limit number of files to copy")
    parser.add_argument("--source", type=str, default=SOURCE_DIR, help="Source directory")
    parser.add_argument("--dest", type=str, default=DEST_DIR, help="Destination directory")

    args = parser.parse_args()
    
    sync_tracks(args.source, args.dest, args.dry_run, args.limit)

if __name__ == "__main__":
    main()
