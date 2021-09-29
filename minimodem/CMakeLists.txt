# set(COMPONENT_SRCS "../minimodem/*.c")

set(COMPONENT_SRCS "${COMPONENT_SRCS}"
"../minimodem/baudot.c" "../minimodem/databits_binary.c" "../minimodem/minimodem_enc_init.c"
"../minimodem/uic_codes.c" "../minimodem/databits_ascii.c" "../minimodem/databits_callerid.c"
"../minimodem/minimodem_encoder.c" "../minimodem/databits_baudot.c" "../minimodem/databits_uic.c"
"../minimodem/simple-tone-generator.c")

#add_library(minimodem baudot.c databits_ascii.c databits_baudot.c
# databits_binary.c databits_callerid.c databits.h databits_uic.c
# fsk.c minimodem.c simpleaudio-benchmark.c simpleaudio.c simpleaudio-pulse.c
# simpleaudio-sndfile.c simple-tone-generator.c uic_codes.c)
set(COMPONENT_ADD_INCLUDEDIRS  "${COMPONENT_ADD_INCLUDEDIRS} ../minimodem/")
#register_component()