/**
 * Replacement for <SDK>/examples/dfu/dfu_public_key.c.
 *
 * That file guards its key array with
 *
 *     #ifdef NRF_DFU_DEBUG_VERSION
 *     ... key ...
 *     #else
 *     #error "Debug public key not valid for production..."
 *     #endif
 *
 * so the stock open_bootloader example only compiles as its *_debug variant or
 * after the user generates a key with nrfutil. Defining NRF_DFU_DEBUG_VERSION
 * to get past it would also arm NRF_BREAKPOINT_COND in the bootloader's fault
 * path, which is not wanted on a board that runs without a debugger attached.
 *
 * The key itself does no work in this build. This is the *open* bootloader:
 * sdk_config.h keeps NRF_DFU_REQUIRE_SIGNED_APP_UPDATE at 0, so
 * signature_required() returns false for application images and the ECDSA
 * verification path in nrf_dfu_validation.c is never entered. What is still
 * checked on every update, signed or not, is the SHA-256 of the image against
 * the hash in the init packet.
 *
 * The symbol cannot simply be dropped: nrf_dfu_validation.c declares
 * "extern const uint8_t pk[64]" at file scope and crypto_init() feeds it to
 * nrf_crypto_ecc_public_key_from_raw(). crypto_init() is reached through
 * nrf_dfu_validation_hash_ok() during any DFU, so the array must exist and must
 * be a well-formed secp256r1 public key for that call to succeed.
 *
 * The bytes below are Nordic's published throwaway debug key, copied verbatim
 * from the SDK file. It is public, it is not secret, and nothing in this
 * configuration trusts it.
 *
 * Consequence, stated plainly: this bootloader accepts unsigned application
 * images over USB, exactly like the one the nRF52840 Dongle ships with. Anyone
 * with physical access to the USB port can reprogram the board.
 */

#include "stdint.h"
#include "compiler_abstraction.h"

/** @brief Public key slot referenced by nrf_dfu_validation.c. Unused for
 *         verification while NRF_DFU_REQUIRE_SIGNED_APP_UPDATE is 0. */
__ALIGN(4) const uint8_t pk[64] =
{
    0x84, 0xb7, 0xac, 0x5d, 0xba, 0x00, 0x1c, 0xcd, 0xbe, 0x49, 0x28, 0xf7, 0xcb, 0xd9, 0x74, 0x44,
    0x5d, 0xa0, 0x84, 0x94, 0xdb, 0x12, 0xa3, 0x6d, 0xb2, 0x4a, 0x17, 0xa1, 0x3d, 0x05, 0xb9, 0x38,
    0xdb, 0xa4, 0x21, 0x45, 0x42, 0x10, 0x1b, 0xbf, 0xeb, 0x09, 0xb5, 0x33, 0x67, 0xab, 0x91, 0x14,
    0x6d, 0xf5, 0xf1, 0x7d, 0xd6, 0x9d, 0x17, 0x88, 0x20, 0xdf, 0xcf, 0xec, 0x86, 0x64, 0x07, 0xc1
};
