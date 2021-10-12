set(COMPONENT_SRCS
        main.c
        http_server.c
        raw_queue.c
        network.c
        pipeline.c
        pipeline_encode.c
        pipeline_decode.c
        pipeline_passthrough.c
        keys.c
        mp3db.c
        tapedb.c
        tapefile.c
        eq.c
        led.c
        )
set(COMPONENT_ADD_INCLUDEDIRS .)

register_component()