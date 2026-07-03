# Audio player module — clip library + alarm tones over the IAudioSink seam
# (modesp/hal/audio_sink.h). Sink backends are DRIVERS (drivers/max98357a)
# registered via MODESP_REGISTER_AUDIO; PlayerModule resolves the active one
# from bindings.json in on_bind. No binding -> NullSink (silent).
idf_component_register(
    SRCS "src/player_module.cpp"
         "src/wav_decoder.cpp"
         "src/mp3_decoder.cpp"
    INCLUDE_DIRS "include"
    # modesp_hal is PUBLIC: player_module.h includes the IAudioSink seam.
    # The MP3 decoder (chmorgan/esp-libhelix-mp3) is pulled via idf_component.yml.
    REQUIRES modesp_core modesp_hal
)
