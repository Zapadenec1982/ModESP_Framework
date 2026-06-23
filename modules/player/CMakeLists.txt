# Audio player module — clip library + alarm tones over an IAudioSink.
#
# Sink backends are COMPILE switches (like display backends): max98357a_sink.cpp
# is an empty TU unless CONFIG_MODESP_AUDIO_MAX98357A (guarded inside the source).
# The ACTIVE sink is chosen in bindings.json ("driver" on role audio_main, module
# player); pins/sample-rate come from board.json i2s_buses. Without a binding the
# player uses NullSink (silent). REQUIRES cannot depend on Kconfig, so esp_driver_i2s
# is required unconditionally; the linker drops unused code when the backend is off.
idf_component_register(
    SRCS "src/player_module.cpp"
         "src/audio_backend_registry.cpp"
         "src/max98357a_sink.cpp"
         "src/wav_decoder.cpp"
         "src/mp3_decoder.cpp"
    INCLUDE_DIRS "include"
    REQUIRES modesp_core
    # modesp_hal: sink factory resolves the I²S bus via HAL::find_i2s_bus +
    # Binding/BindingTable in bind_audio. esp_driver_i2s/driver: I²S + GPIO. .cpp-only → PRIV.
    # The MP3 decoder (chmorgan/esp-libhelix-mp3) is pulled via idf_component.yml;
    # the component manager auto-adds it to this component's requirements.
    PRIV_REQUIRES modesp_hal esp_driver_i2s esp_driver_gpio driver
)
