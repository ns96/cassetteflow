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
        audiodb.c
        tapedb.c
        tapefile.c
        eq.c
        led.c
        mp3info.c
        filter_line_reader.c
        bt.c
        pipeline_output.c
        pipeline_playback.c
        volume.c
        )
set(COMPONENT_ADD_INCLUDEDIRS .)

register_component()