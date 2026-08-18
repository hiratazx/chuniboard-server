#pragma once

#include <windows.h>

#include <stddef.h>
#include <stdint.h>

/*
    Get the version of the Aime IO API that this DLL supports. This function
    should return a positive 16-bit integer, where the high byte is the major
    version and the low byte is the minor version (as defined by the Semantic
    Versioning standard).

    The latest API version as of this writing is 0x0101.
 */
uint16_t aime_io_get_api_version(void);

/*
    Initialize Aime IO provider DLL. Only called once, before any other
    functions exported from this DLL are called (except for
    aime_io_get_api_version).

    Minimum API version: 0x0100
 */
HRESULT aime_io_init(void);

/*
    Poll for IC cards in the vicinity.

    - unit_no: 0 on the primary Aime reader (most games), may be 1 on Sangokushi Taisen for the queue reader

    Minimum API version: 0x0100
 */
HRESULT aime_io_nfc_poll(uint8_t unit_no);

/*
    Attempt to read out a classic Aime card ID

    - unit_no: 0 on the primary Aime reader (most games), may be 1 on Sangokushi Taisen for the queue reader
    - luid: Pointer to a ten-byte buffer that will receive the ID
    - luid_size: Size of the buffer at *luid. Always 10.

    Returns:

    - S_OK if a classic Aime is present and was read successfully
    - S_FALSE if no classic Aime card is present (*luid will be ignored)
    - Any HRESULT error if an error occured.

    Minimum API version: 0x0100
*/
HRESULT aime_io_nfc_get_aime_id(
        uint8_t unit_no,
        uint8_t *luid,
        size_t luid_size);

/*
    Attempt to read out a FeliCa card ID ("IDm"). The following are examples
    of FeliCa cards:

    - Amuse IC (which includes new-style Aime-branded cards, among others)
    - Smartphones with FeliCa NFC capability (uncommon outside Japan)
    - Various Japanese e-cash cards and train passes

    Parameters:

    - unit_no: 0 on the primary Aime reader (most games), may be 1 on Sangokushi Taisen for the queue reader
    - IDm: Output parameter that will receive the card ID

    Returns:

    - S_OK if a FeliCa device is present and was read successfully
    - S_FALSE if no FeliCa device is present (*IDm will be ignored)
    - Any HRESULT error if an error occured.

    Minimum API version: 0x0100
*/
HRESULT aime_io_nfc_get_felica_id(uint8_t unit_no, uint64_t *IDm);

/*
    MIFARE key selector values used by the set key/authenticate functions.

    Minimum API version: 0x0101
*/
enum {
    AIME_IO_MIFARE_KEY_AIME = 0,
    AIME_IO_MIFARE_KEY_BANA = 1,
};

/*
    Attempt to read the 4-byte MIFARE UID of the currently present card.

    Parameters:

    - unit_no: Always 0 as of the current API version
    - uid: Pointer to a four-byte buffer that will receive the UID
    - uid_size: Size of the buffer at *uid. Always 4.

    Returns:

    - S_OK if a MIFARE card is present and the UID was read successfully
    - S_FALSE if no MIFARE card is present (*uid will be ignored)
    - Any HRESULT error if an error occured.

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_get_mifare_uid(
        uint8_t unit_no,
        uint8_t *uid,
        size_t uid_size);

/*
    Select a MIFARE card by UID (optional for real readers).

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_mifare_select(
        uint8_t unit_no,
        const uint8_t *uid,
        size_t uid_size);

/*
    Supply a MIFARE authentication key to the reader.

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_mifare_set_key(
        uint8_t unit_no,
        uint8_t key_type,
        const uint8_t *key,
        size_t key_size);

/*
    Perform a MIFARE authentication sequence.

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_mifare_authenticate(
        uint8_t unit_no,
        uint8_t key_type,
        const uint8_t *payload,
        size_t payload_size);

/*
    Read a 16-byte MIFARE block from the card.

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_mifare_read_block(
        uint8_t unit_no,
        const uint8_t *uid,
        size_t uid_size,
        uint8_t block_no,
        uint8_t *block,
        size_t block_size);

/*
    Forward a raw FeliCa request to a real reader.

    Parameters:

    - req: FeliCa request buffer, including the length byte
    - res: FeliCa response buffer, including the length byte
    - res_size_written: Output size of the response (bytes written to *res)

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_felica_transact(
        uint8_t unit_no,
        const uint8_t *req,
        size_t req_size,
        uint8_t *res,
        size_t res_size,
        size_t *res_size_written);

/*
    Enable the reader's RF field.

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_radio_on(uint8_t unit_no);

/*
    Disable the reader's RF field.

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_radio_off(uint8_t unit_no);

/*
    Put the reader into firmware update mode.

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_to_update_mode(uint8_t unit_no);

/*
    Forward a raw hex-data command to the reader.

    Parameters:

    - payload: Command payload bytes
    - payload_size: Size of the payload
    - status_out: Optional pointer to receive the SG status byte

    Minimum API version: 0x0101
*/
HRESULT aime_io_nfc_send_hex_data(
        uint8_t unit_no,
        const uint8_t *payload,
        size_t payload_size,
        uint8_t *status_out);

/*
    Change the color and brightness of the card reader's RGB lighting

    - unit_no: 0 on the primary Aime reader (most games), may be 1 on Sangokushi Taisen for the queue reader
    - r, g, b: Primary color intensity, from 0 to 255 inclusive.

    Minimum API version: 0x0100
*/
void aime_io_led_set_color(uint8_t unit_no, uint8_t r, uint8_t g, uint8_t b);

/*
    VFD text forwarding. This is intended to pass through the text payload
    plus the most recent VFD state so external handlers can emulate scrolling
    or layout if desired.

    - text: Pointer to raw text bytes (not null-terminated)
    - text_len: Length of the text buffer
    - state: Current VFD state at the time of rendering

    The encoding field uses VFD encoding values (0=GB2312, 1=Big5,
    2=Shift-JIS, 3=KSC5601).

    Minimum API version: 0x0101
*/
struct aime_io_vfd_state {
    uint8_t encoding;
    uint8_t text_speed;
    uint8_t scroll_enabled;
    uint16_t h_scroll;
    uint16_t cursor_x;
    uint8_t cursor_y;
    uint16_t wnd_x0;
    uint8_t wnd_y0;
    uint16_t wnd_x1;
    uint8_t wnd_y1;
    uint8_t rotate;
    uint8_t brightness;
    uint8_t screen_on;
    uint32_t clear_seq;
};

void aime_io_vfd_set_text(
        const uint8_t *text,
        size_t text_len,
        const struct aime_io_vfd_state *state);

/*
    VFD state change notification. Called when the VFD state changes even
    when no text is written.

    Minimum API version: 0x0101
*/
void aime_io_vfd_set_state(const struct aime_io_vfd_state *state);
