# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly

set(COMPONENT_SRCDIRS  api dft dft/scalar dft/scalar/codelets kernel libbench2
        rdft rdft/scalar rdft/scalar/r2cb rdft/scalar/r2cf rdft/scalar/r2r reodft)
set(COMPONENT_ADD_INCLUDEDIRS . api dft dft/scalar dft/scalar/codelets kernel libbench2
        rdft rdft/scalar rdft/scalar/r2cb rdft/scalar/r2cf rdft/scalar/r2r reodft)

#set(COMPONENT_REQUIRES esp-adf-libs audio_stream)

register_component()

# fftw3/dft/scalar/codelets/n1_3.c:93:122: error: right-hand operand of comma expression has no effect [-Werror=unused-value]
component_compile_options(-Wno-unused-value)
