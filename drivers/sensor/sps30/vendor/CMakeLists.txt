# TODO
# Linking with library files from these repos:
#   embedded-common (https://github.com/Sensirion/embedded-common)
#   embedded-sps    (https://github.com/Sensirion/embedded-sps)
#
# A zephyr i2c implementation has already been written here:
#   https://github.com/Sensirion/embedded-common/blob/master/i2c/sample-implementations/zephyr_user_space/sensirion_i2c_hal.c

zephyr_library_named(zephyr_vendor)
zephyr_library_sources(sensirion_common.c sensirion_i2c.c sps30.c)
