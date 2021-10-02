set(COMPONENT_SRCS
        main.c
        http_server.c
        network.c
        pipeline_encode.c
        pipeline_decode.c
        keys.c
        mp3db.c
        tapedb.c
        )
set(COMPONENT_ADD_INCLUDEDIRS .)

register_component()