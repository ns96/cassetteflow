set(COMPONENT_SRCS "baudot.c" "databits_binary.c" "minimodem_enc_init.c"
        "uic_codes.c" "databits_ascii.c" "databits_callerid.c"
        "minimodem_encoder.c" "databits_baudot.c" "databits_uic.c"
        "simple-tone-generator.c"
        "minimodem_decoder.c" "minimodem_dec_init.c" "fsk.c"
        )
set(COMPONENT_ADD_INCLUDEDIRS .)

set(COMPONENT_PRIV_REQUIRES fftw3)
set(COMPONENT_REQUIRES esp-adf-libs audio_stream)

register_component()
