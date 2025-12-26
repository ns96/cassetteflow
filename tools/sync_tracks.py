import os
import shutil
import argparse
import time
from pathlib import Path
import platform

# ==========================================
# Configuration
# ==========================================
SOURCE_DIR = r"C:\mp3files" 
DEST_DIR = r"D:/"
# ==========================================

def get_filename_from_path(path_str):
    path_str = path_str.replace('\\', '/')
    return path_str.split('/')[-1]

def parse_audiodb(db_path):
    allowed_files = set()
    if not os.path.exists(db_path):
        print(f"Error: audiodb.txt not found at {db_path}")
        return allowed_files

    print(f"Parsing DB: {db_path}")
    try:
        with open(db_path, 'r', encoding='utf-8') as f:
            for line in f:
                if not line.strip(): continue
                parts = line.strip().split('\t')
                if len(parts) >= 4:
                    full_path = parts[3]
                    filename = get_filename_from_path(full_path).strip()
                    if filename:
                        allowed_files.add(filename.lower())
    except Exception as e:
        print(f"Error reading audiodb.txt: {e}")
    
    return allowed_files

def format_size(size_bytes):
    gb = size_bytes / (1024**3)
    if gb >= 0.01:
        return f"{gb:.2f} GB"
    mb = size_bytes / (1024**2)
    return f"{mb:.2f} MB"

def sync_tracks(source_root, dest_root, dry_run=False, limit=None):
    start_time = time.time()
    
    db_path = os.path.join(source_root, 'audiodb.txt')
    allowed_files = parse_audiodb(db_path)
    print(f"DEBUG: Total distinct Allowed Filenames in DB: {len(allowed_files)}")
    if limit:
        print(f"DEBUG: Limit set to {limit} files")

    files_found = 0
    files_copied = 0
    
    # Track unique filenames we have actually processed/copied to destination
    # to avoid duplicates and ensure copied_count <= allowed_count
    processed_filenames = set()
    collisions = []

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

    for root, dirs, files in os.walk(source_root):
        if limit and files_copied >= limit:
            break
            
        for file in files:
            if limit and files_copied >= limit:
                break
                
            ext = os.path.splitext(file)[1].lower()
            if ext in ['.mp3', '.flac']:
                files_found += 1
                
                fname_lower = file.lower()
                
                if fname_lower in allowed_files:
                    # Check for collision (have we already seen this filename?)
                    if fname_lower in processed_filenames:
                        src_path_display = os.path.join(root, file)
                        collisions.append(src_path_display)
                        continue

                    # Mark as processed
                    processed_filenames.add(fname_lower)

                    src_file = os.path.join(root, file)
                    dst_file = os.path.join(dest_root, file)
                    
                    try:
                        fsize = os.path.getsize(src_file)
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
                        files_copied += 1
                    else:
                        try:
                            shutil.copy2(src_file, dst_file)
                            files_copied += 1
                        except Exception as e:
                            print(f"Failed to copy {file}: {e}")
                    
                    if files_copied > 0 and files_copied % 100 == 0:
                        print(f"Progress: {files_copied} files {'scanned (Dry Run)' if dry_run else 'copied'}...")

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
    print(f"Files Allowed (DB): {len(allowed_files)}")
    print(f"Files Found (Scanned): {files_found}")
    print("-" * 40)
    print("Files Copied Details:")
    print(f"  MP3:  {stats['mp3']['count']:>5} files | {format_size(stats['mp3']['bytes'])}")
    print(f"  FLAC: {stats['flac']['count']:>5} files | {format_size(stats['flac']['bytes'])}")
    print("-" * 40)
    print(f"TOTAL:  {files_copied:>5} files | {format_size(total_bytes)}")
    print(f"Collisions Skipped: {len(collisions)}")
    if len(collisions) > 0:
        print("  (Duplicate filenames found in source were skipped to prevent overwrite)")
    print(f"Time Elapsed:       {duration:.2f} seconds")
    print("="*40)

def main():
    parser = argparse.ArgumentParser(description="Sync MP3/FLAC files to SD card based on audiodb.txt")
    parser.add_argument("--dry-run", action="store_true", help="Scan and list files without copying")
    parser.add_argument("--limit", type=int, help="Limit number of files to copy (for testing)")

    args = parser.parse_args()
    
    if "PUT_SOURCE_PATH_HERE" in SOURCE_DIR:
        print("CRITICAL: You must set SOURCE_DIR in the script")
        return

    sync_tracks(SOURCE_DIR, DEST_DIR, args.dry_run, args.limit)

if __name__ == "__main__":
    main()
