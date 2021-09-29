# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

set(COMPONENT_SRCS "baudot.c" "databits_binary.c" "minimodem_enc_init.c"
        "uic_codes.c" "databits_ascii.c" "databits_callerid.c"
        "minimodem_encoder.c" "databits_baudot.c" "databits_uic.c"
        "simple-tone-generator.c")
set(COMPONENT_ADD_INCLUDEDIRS .)
#set(COMPONENT_EMBED_TXTFILES 	xml/ConnectionManager.xml
#        xml/EventProtocolCMR.xml)

set(COMPONENT_REQUIRES esp-adf-libs audio_stream)

register_component()
