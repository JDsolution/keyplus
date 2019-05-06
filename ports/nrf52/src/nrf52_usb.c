/**
 * Copyright (c) 2016 - 2019, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "nrf.h"
#include "nrf_drv_usbd.h"
#include "nrf_drv_clock.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_drv_power.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "app_timer.h"
#include "app_error.h"
#include "bsp.h"

#include "usb/descriptors.h"
#include "core/flash.h"
#include "core/settings.h"
#include "core/timer.h"

#include "serial_num.h"

static bool m_send_flag = 0;
static bool m_mouse_dir = 0;

/**
 * @brief Configuration status LED
 *
 * This LED would blink quickly (5&nbsp;Hz) when device is not configured
 * or slowly (1&nbsp;Hz) when configured and working properly.
 */
#define LED_USB_STATUS BSP_BOARD_LED_0
/**
 * @brief Enable power USB detection
 *
 * Configure if example supports USB port connection
 */
#ifndef USBD_POWER_DETECTION
#define USBD_POWER_DETECTION true
#endif

/**
 * @brief Startup delay
 *
 * Number of microseconds to start USBD after powering up.
 * Kind of port insert debouncing.
 */
#define STARTUP_DELAY 100

/** Maximum size of the packed transfered by EP0 */
#define EP0_MAXPACKETSIZE NRF_DRV_USBD_EPSIZE

/** Configuration descriptor */
#define DEVICE_SELF_POWERED 1
#define REMOTE_WU           1

/**
 * String config descriptor
 */
#define USBD_STRING_LANG_IX  0x00
#define USBD_STRING_MANUFACTURER_IX  0x01
#define USBD_STRING_PRODUCT_IX  0x02
#define USBD_STRING_SERIAL_IX  0x03

static const uint8_t get_config_resp_configured[] = { 1 };
static const uint8_t get_config_resp_unconfigured[] = { 0 };

static const uint8_t get_status_device_resp_nrwu[] = {
    ((DEVICE_SELF_POWERED) ? 1 : 0),    // LSB first: self-powered, no
    // remoteWk
    0
};

static const uint8_t get_status_device_resp_rwu[] = {
    ((DEVICE_SELF_POWERED) ? 1 : 0) | 2,    // LSB first:
    // self-powered, remoteWk
    0
};

static const uint8_t get_status_interface_resp[] = { 0, 0 };
static const uint8_t get_status_ep_halted_resp[] = { 1, 0 };
static const uint8_t get_status_ep_active_resp[] = { 0, 0 };

/**
 * @brief USB configured flag
 *
 * The flag that is used to mark the fact that USB is configured and ready
 * to transmit data
 */
static volatile bool m_usbd_configured = false;

/**
 * @brief USB suspended
 *
 * The flag that is used to mark the fact that USB is suspended and requires wake up
 * if new data is available.
 *
 * @note This variable is changed from the main loop.
 */
static bool m_usbd_suspended = false;

/**
 * @brief Mark the fact if remote wake up is enabled
 *
 * The internal flag that marks if host enabled the remote wake up functionality in this device.
 */
static
#if REMOTE_WU
    volatile            // Disallow optimization only if Remote
                // wakeup is enabled
#endif
bool m_usbd_rwu_enabled = false;

/**
 * @brief The requested suspend state
 *
 * The currently requested suspend state based on the events
 * received from USBD library.
 * If the value here is different than the @ref m_usbd_suspended
 * the state changing would be processed inside main loop.
 */
static volatile bool m_usbd_suspend_state_req = false;

/**
 * @brief The flag for mouse position send pending
 *
 * Setting this flag means that USB endpoint is busy by sending
 * last mouse position.
 */
static volatile bool m_send_mouse_position = false;


/**
 * @brief Setup all the endpoints for selected configuration
 *
 * Function sets all the endpoints for specific configuration.
 *
 * @note
 * Setting the configuration index 0 means technically disabling the HID interface.
 * Such configuration should be set when device is starting or USB reset is detected.
 *
 * @param index Configuration index
 *
 * @retval NRF_ERROR_INVALID_PARAM Invalid configuration
 * @retval NRF_SUCCESS             Configuration successfully set
 */
static ret_code_t ep_configuration(uint8_t index)
{
    if (index == 1) {
        // boot keyboard endpoint
        nrf_drv_usbd_ep_dtoggle_clear(NRF_DRV_USBD_EPIN1);
        nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN1);
        nrf_drv_usbd_ep_enable(NRF_DRV_USBD_EPIN1);

        // mouse endpoint
        nrf_drv_usbd_ep_dtoggle_clear(NRF_DRV_USBD_EPIN2);
        nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN2);
        nrf_drv_usbd_ep_enable(NRF_DRV_USBD_EPIN2);

        // media endpoint
        nrf_drv_usbd_ep_dtoggle_clear(NRF_DRV_USBD_EPIN3);
        nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN3);
        nrf_drv_usbd_ep_enable(NRF_DRV_USBD_EPIN3);

        // vendor endpoints
        nrf_drv_usbd_ep_dtoggle_clear(NRF_DRV_USBD_EPIN4);
        nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN4);
        nrf_drv_usbd_ep_enable(NRF_DRV_USBD_EPIN4);

        nrf_drv_usbd_ep_dtoggle_clear(NRF_DRV_USBD_EPOUT4);
        nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPOUT4);
        nrf_drv_usbd_ep_enable(NRF_DRV_USBD_EPOUT4);

        // nkro endpoint
        nrf_drv_usbd_ep_dtoggle_clear(NRF_DRV_USBD_EPIN5);
        nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN5);
        nrf_drv_usbd_ep_enable(NRF_DRV_USBD_EPIN5);

        m_usbd_configured = true;
        nrf_drv_usbd_setup_clear();
    } else if (index == 0) {
        nrf_drv_usbd_ep_disable(NRF_DRV_USBD_EPIN1);
        nrf_drv_usbd_ep_disable(NRF_DRV_USBD_EPIN2);
        nrf_drv_usbd_ep_disable(NRF_DRV_USBD_EPIN3);
        nrf_drv_usbd_ep_disable(NRF_DRV_USBD_EPIN4);
        nrf_drv_usbd_ep_disable(NRF_DRV_USBD_EPOUT4);
        nrf_drv_usbd_ep_disable(NRF_DRV_USBD_EPIN5);

        m_usbd_configured = false;
        nrf_drv_usbd_setup_clear();
    } else {
        return NRF_ERROR_INVALID_PARAM;
    }
    return NRF_SUCCESS;
}

/**
 * @name Processing setup requests
 *
 * @{
 */
/**
 * @brief Respond on ep 0
 *
 * Auxiliary function for sending respond on endpoint 0
 * @param[in] p_setup Pointer to setup data from current setup request.
 *                    It would be used to calculate the size of data to send.
 * @param[in] p_data  Pointer to the data to send.
 * @param[in] size    Number of bytes to send.
 * @note Data pointed by p_data has to be available till the USBD_EVT_BUFREADY event.
 */
static void
respond_setup_data(nrf_drv_usbd_setup_t const *const p_setup,
           void const *p_data, size_t size)
{
    /*
     * Check the size against required response size
     */
    if (size > p_setup->wLength) {
        size = p_setup->wLength;
    }
    ret_code_t ret;
    nrf_drv_usbd_transfer_t transfer = {
        .p_data = {.tx = p_data},
        .size = size
    };
    ret = nrf_drv_usbd_ep_transfer(NRF_DRV_USBD_EPIN0, &transfer);
    if (ret != NRF_SUCCESS) {
        NRF_LOG_ERROR("Transfer starting failed: %d", (uint32_t) ret);
    }
    ASSERT(ret == NRF_SUCCESS);
    UNUSED_VARIABLE(ret);
}

/** React to GetStatus */
static void usbd_setup_GetStatus(nrf_drv_usbd_setup_t const *const p_setup)
{
    switch (p_setup->bmRequestType) {
    case 0x80:      // Device
        if (((p_setup->wIndex) & 0xff) == 0) {
            respond_setup_data(p_setup,
                       m_usbd_rwu_enabled ?
                       get_status_device_resp_rwu :
                       get_status_device_resp_nrwu,
                       sizeof(get_status_device_resp_nrwu));
            return;
        }
        break;
    case 0x81:      // Interface
        if (m_usbd_configured)  // Respond only if configured
        {
            if (((p_setup->wIndex) & 0xff) == 0)    // Only interface
                // 0 supported
            {
                respond_setup_data(p_setup,
                           get_status_interface_resp,
                           sizeof
                           (get_status_interface_resp));
                return;
            }
        }
        break;
    case 0x82:      // Endpoint
        if (((p_setup->wIndex) & 0xff) == 0)    // Endpoint 0
        {
            respond_setup_data(p_setup,
                       get_status_ep_active_resp,
                       sizeof(get_status_ep_active_resp));
            return;
        }
        if (m_usbd_configured)  // Other endpoints responds if configured
        {
            if (((p_setup->wIndex) & 0xff) == NRF_DRV_USBD_EPIN1) {
                if (nrf_drv_usbd_ep_stall_check
                    (NRF_DRV_USBD_EPIN1)) {
                    respond_setup_data(p_setup,
                               get_status_ep_halted_resp,
                               sizeof
                               (get_status_ep_halted_resp));
                    return;
                } else {
                    respond_setup_data(p_setup,
                               get_status_ep_active_resp,
                               sizeof
                               (get_status_ep_active_resp));
                    return;
                }
            }
        }
        break;
    default:
        break;      // Just go to stall
    }
    NRF_LOG_ERROR("Unknown status: 0x%2x", p_setup->bmRequestType);
    nrf_drv_usbd_setup_stall();
}

static void usbd_setup_ClearFeature(nrf_drv_usbd_setup_t const *const p_setup)
{
    if ((p_setup->bmRequestType) == 0x02)   // standard request,
        // recipient=endpoint
    {
        if ((p_setup->wValue) == 0) {
            if ((p_setup->wIndex) == NRF_DRV_USBD_EPIN1) {
                nrf_drv_usbd_ep_stall_clear(NRF_DRV_USBD_EPIN1);
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    } else if ((p_setup->bmRequestType) == 0x0) // standard request,
        // recipient=device
    {
        if (REMOTE_WU) {
            if ((p_setup->wValue) == 1) // Feature Wakeup
            {
                m_usbd_rwu_enabled = false;
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    }
    NRF_LOG_ERROR("Unknown feature to clear");
    nrf_drv_usbd_setup_stall();
}

static void usbd_setup_SetFeature(nrf_drv_usbd_setup_t const *const p_setup)
{
    if ((p_setup->bmRequestType) == 0x02)   // standard request,
        // recipient=endpoint
    {
        if ((p_setup->wValue) == 0) // Feature HALT
        {
            if ((p_setup->wIndex) == NRF_DRV_USBD_EPIN1) {
                nrf_drv_usbd_ep_stall(NRF_DRV_USBD_EPIN1);
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    } else if ((p_setup->bmRequestType) == 0x0) // standard request,
        // recipient=device
    {
        if (REMOTE_WU) {
            if ((p_setup->wValue) == 1) // Feature Wakeup
            {
                m_usbd_rwu_enabled = true;
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    }
    NRF_LOG_ERROR("Unknown feature to set");
    nrf_drv_usbd_setup_stall();
}

static void usbd_setup_GetDescriptor(nrf_drv_usbd_setup_t const *const p_setup)
{
    // determine which descriptor has been asked for
    switch ((p_setup->wValue) >> 8) {
    case 1:     // Device
        if ((p_setup->bmRequestType) == 0x80) {
            respond_setup_data(
                p_setup,
                &usb_device_desc,
                sizeof(usb_device_desc)
            );
            return;
        }
        break;
    case 2:     // Configuration
        if ((p_setup->bmRequestType) == 0x80) {
            respond_setup_data(
                p_setup,
                &usb_config_desc,
                sizeof(usb_config_desc)
            );
            return;
        }
        break;
    case 3:     // String
        if ((p_setup->bmRequestType) == 0x80) {
            // Select the string
            switch ((p_setup->wValue) & 0xFF) {
            case USBD_STRING_LANG_IX: {
                const uint8_t len = usb_string_desc_0[0] & 0xff;
                respond_setup_data(p_setup, usb_string_desc_0, len);
                return;
            } break;
            case USBD_STRING_MANUFACTURER_IX: {
                const uint8_t len = usb_string_desc_1[0] & 0xff;
                respond_setup_data(p_setup, usb_string_desc_1, len);
                return;
            } break;
            case USBD_STRING_PRODUCT_IX: {
                uint8_t len;
                // First byte of the string desc which is its length
                flash_read(
                    &len,
                    (flash_ptr_t)(GET_SETTING(device_name)),
                    1
                );

                // Check length is not too big
                if (len <= SETTINGS_NAME_STORAGE_SIZE) {
                    respond_setup_data(
                        p_setup,
                        GET_SETTING(device_name),
                        len);
                    return;
                }
            } break;

            // Serial number
            case USBD_STRING_SERIAL_IX: {
                respond_setup_data(p_setup,
                                   g_nrf52_serial_usb_desc,
                                   sizeof(g_nrf52_serial_usb_desc));
                return;
            } break;

            default:
                break;
            }
        }
        break;
    // case 4:     // Interface
    //     if ((p_setup->bmRequestType) == 0x80) {
    //         // Which interface?
    //         uint8_t intf_num = ((p_setup->wValue) & 0xFF);
    //         switch (intf_num) {
    //             case INTERFACE_BOOT_KEYBOARD:
    //                 respond_setup_data(
    //                     p_setup,
    //                     &usb_config_desc.intf0,
    //                     sizeof(usb_interface_desc_t)
    //                 );
    //                 return;
    //         }
    //     }
    //     break;
    // case 5:     // Endpoint
    //     if ((p_setup->bmRequestType) == 0x80) {
    //         // Which endpoint?
    //         if (((p_setup->wValue) & 0xFF) == 1) {
    //             respond_setup_data(p_setup,
    //                        get_descriptor_endpoint_1,
    //                        GET_ENDPOINT_DESC_SIZE);
    //             return;
    //         }
    //     }
    //     break;
    // case 0x21:      // HID
    //     if ((p_setup->bmRequestType) == 0x81) {
    //         // Which interface
    //         if (((p_setup->wValue) & 0xFF) == 0) {
    //             respond_setup_data(p_setup,
    //                        get_descriptor_hid_0,
    //                        GET_HID_DESC_SIZE);
    //             return;
    //         }
    //     }
    //     break;
    case 0x22:      // HID report
        if ((p_setup->bmRequestType) == 0x81) {
            // Which interface?
            uint8_t intf = ((p_setup->wIndex) & 0xFF);
            switch (intf) {
            case INTERFACE_BOOT_KEYBOARD: {
                respond_setup_data(
                    p_setup,
                    hid_desc_boot_keyboard,
                    sizeof_hid_desc_boot_keyboard
                );
                return;
            } break;

            case INTERFACE_MOUSE: {
                respond_setup_data(
                    p_setup,
                    hid_desc_mouse,
                    sizeof_hid_desc_mouse
                );
                return;
            } break;

            case INTERFACE_MEDIA: {
                respond_setup_data(
                    p_setup,
                    hid_desc_media,
                    sizeof_hid_desc_media
                );
                return;
            } break;

            case INTERFACE_VENDOR: {
                respond_setup_data(
                    p_setup,
                    hid_desc_vendor,
                    sizeof_hid_desc_vendor
                );
                return;
            } break;

            case INTERFACE_NKRO_KEYBOARD: {
                respond_setup_data(
                    p_setup,
                    hid_desc_nkro_keyboard,
                    sizeof_hid_desc_nkro_keyboard
                );
                return;
            } break;

            default:
                break;
            }
        }
        break;
    default:
        break;      // Not supported - go to stall
    }

    NRF_LOG_ERROR
        ("Unknown descriptor requested: 0x%02x, type: 0x%02x or value: 0x%02x",
         p_setup->wValue >> 8, p_setup->bmRequestType,
         p_setup->wValue & 0xFF);
    nrf_drv_usbd_setup_stall();
}

static void usbd_setup_GetConfig(nrf_drv_usbd_setup_t const *const p_setup)
{
    if (m_usbd_configured) {
        respond_setup_data(p_setup,
                   get_config_resp_configured,
                   sizeof(get_config_resp_configured));
    } else {
        respond_setup_data(p_setup,
                   get_config_resp_unconfigured,
                   sizeof(get_config_resp_unconfigured));
    }
}

static void usbd_setup_SetConfig(nrf_drv_usbd_setup_t const *const p_setup)
{
    if ((p_setup->bmRequestType) == 0x00) {
        // accept only 0 and 1
        if (((p_setup->wIndex) == 0) && ((p_setup->wLength) == 0) &&
            ((p_setup->wValue) <= UINT8_MAX)) {
            if (NRF_SUCCESS ==
                ep_configuration((uint8_t) (p_setup->wValue))) {
                nrf_drv_usbd_setup_clear();
                return;
            }
        }
    }
    NRF_LOG_ERROR("Wrong configuration: Index: 0x%2x, Value: 0x%2x, "
        "bmRequestType: 0x%02x, bRequest: 0x%02x.",
              p_setup->wIndex, p_setup->wValue, p_setup->bmRequestType, p_setup->bRequest);
    nrf_drv_usbd_setup_stall();
}

static void usbd_setup_SetIdle(nrf_drv_usbd_setup_t const *const p_setup)
{
    if (p_setup->bmRequestType == 0x21) {
        // accept any value
        nrf_drv_usbd_setup_clear();
        return;
    }
    NRF_LOG_ERROR("Set Idle wrong type: 0x%2x.", p_setup->bmRequestType);
    nrf_drv_usbd_setup_stall();
}

static void usbd_setup_SetInterface(nrf_drv_usbd_setup_t const *const p_setup)
{
    // no alternate setting is supported - STALL always
    NRF_LOG_ERROR("No alternate interfaces supported.");
    nrf_drv_usbd_setup_stall();
}

static void usbd_setup_SetProtocol(nrf_drv_usbd_setup_t const *const p_setup)
{
    if (p_setup->bmRequestType == 0x21) {
        // accept any value
        nrf_drv_usbd_setup_clear();
        return;
    }
    NRF_LOG_ERROR("Set Protocol wrong type: 0x%2x.",
              p_setup->bmRequestType);
    nrf_drv_usbd_setup_stall();
}

                          /** @} *//*
                           * End of processing setup requests
                           * functions
                           */

static void usbd_event_handler(nrf_drv_usbd_evt_t const *const p_event)
{
    switch (p_event->type) {
    case NRF_DRV_USBD_EVT_SUSPEND:
        NRF_LOG_INFO("SUSPEND state detected");
        m_usbd_suspend_state_req = true;
        break;
    case NRF_DRV_USBD_EVT_RESUME:
        NRF_LOG_INFO("RESUMING from suspend");
        m_usbd_suspend_state_req = false;
        break;
    case NRF_DRV_USBD_EVT_WUREQ:
        NRF_LOG_INFO("RemoteWU initiated");
        m_usbd_suspend_state_req = false;
        break;
    case NRF_DRV_USBD_EVT_RESET:
        {
            ret_code_t ret = ep_configuration(0);
            ASSERT(ret == NRF_SUCCESS);
            UNUSED_VARIABLE(ret);
            m_usbd_suspend_state_req = false;
            break;
        }
    case NRF_DRV_USBD_EVT_SOF:
        {
            static uint32_t cycle = 0;
            ++cycle;
            if ((cycle % (m_usbd_configured ? 500 : 100)) == 0) {
                bsp_board_led_invert(LED_USB_STATUS);
            }
            break;
        }
    case NRF_DRV_USBD_EVT_EPTRANSFER:
        if (NRF_DRV_USBD_EPIN1 == p_event->data.eptransfer.ep) {
        } else if (NRF_DRV_USBD_EPIN2 == p_event->data.eptransfer.ep) {
            m_send_mouse_position = false;
        } else if (NRF_DRV_USBD_EPIN0 == p_event->data.eptransfer.ep) {
            if (NRF_USBD_EP_OK == p_event->data.eptransfer.status) {
                if (!nrf_drv_usbd_errata_154()) {
                    /*
                     * Transfer ok - allow status stage
                     */
                    nrf_drv_usbd_setup_clear();
                }
            } else if (NRF_USBD_EP_ABORTED ==
                   p_event->data.eptransfer.status) {
                /*
                 * Just ignore
                 */
                NRF_LOG_INFO("Transfer aborted event on EPIN0");
            } else {
                NRF_LOG_ERROR("Transfer failed on EPIN0: %d",
                          p_event->data.eptransfer.status);
                nrf_drv_usbd_setup_stall();
            }
        } else if (NRF_DRV_USBD_EPOUT0 == p_event->data.eptransfer.ep) {
            /*
             * NOTE: No EPOUT0 data transfers are used. The code is here
             * as a pattern how to support such a transfer.
             */
            if (NRF_USBD_EP_OK == p_event->data.eptransfer.status) {
                /*
                 * NOTE: Data values or size may be tested here to decide
                 * if clear or stall. If errata 154 is present the data
                 * transfer is acknowledged by the hardware.
                 */
                if (!nrf_drv_usbd_errata_154()) {
                    /*
                     * Transfer ok - allow status stage
                     */
                    nrf_drv_usbd_setup_clear();
                }
            } else if (NRF_USBD_EP_ABORTED ==
                   p_event->data.eptransfer.status) {
                /*
                 * Just ignore
                 */
                NRF_LOG_INFO
                    ("Transfer aborted event on EPOUT0");
            } else {
                NRF_LOG_ERROR("Transfer failed on EPOUT0: %d",
                          p_event->data.eptransfer.status);
                nrf_drv_usbd_setup_stall();
            }
        } else {
            /*
             * Nothing to do
             */
        }
        break;
    case NRF_DRV_USBD_EVT_SETUP:
        {
            nrf_drv_usbd_setup_t setup;
            nrf_drv_usbd_setup_get(&setup);
            switch (setup.bRequest) {
            case 0x00:  // GetStatus
                usbd_setup_GetStatus(&setup);
                break;
            case 0x01:  // CleartFeature
                usbd_setup_ClearFeature(&setup);
                break;
            case 0x03:  // SetFeature
                usbd_setup_SetFeature(&setup);
                break;
            case 0x05:  // SetAddress
                // nothing to do, handled by hardware; but don't STALL
                break;
            case 0x06:  // GetDescriptor
                usbd_setup_GetDescriptor(&setup);
                break;
            case 0x08:  // GetConfig
                usbd_setup_GetConfig(&setup);
                break;
            case 0x09:  // SetConfig
                usbd_setup_SetConfig(&setup);
                break;
                // HID class
            case 0x0A:  // SetIdle
                usbd_setup_SetIdle(&setup);
                break;
            case 0x0B:  // SetProtocol or SetInterface
                if (setup.bmRequestType == 0x01)    // standard
                    // request,
                    // recipient=interface
                {
                    usbd_setup_SetInterface(&setup);
                } else if (setup.bmRequestType == 0x21) // class request,
                    // recipient=interface
                {
                    usbd_setup_SetProtocol(&setup);
                } else {
                    NRF_LOG_ERROR
                        ("Command 0xB. Unknown request: 0x%02x",
                         setup.bmRequestType);
                    nrf_drv_usbd_setup_stall();
                }
                break;
            default:
                NRF_LOG_ERROR("Unknown request: 0x%02x",
                          setup.bRequest);
                nrf_drv_usbd_setup_stall();
                return;
            }
            break;
        }
    default:
        break;
    }
}

#include "usb_reports/mouse_report.h"

#include "usb_reports/usb_reports.h"

static void move_mouse_pointer(void)
{

    if (!m_usbd_configured)
        return;
    if (!m_send_mouse_position) {
        hid_report_mouse_t report = { 0 };

        if ((m_mouse_dir & 1) == 0) {
            report.x = -1;
        } else {
            report.x = 1;
        };

        usb_write_in_endpoint(
            EP_NUM_MOUSE,
            (const uint8_t *)&report,
            sizeof(report)
        );
    }
}

static void power_usb_event_handler(nrf_drv_power_usb_evt_t event)
{
    switch (event) {
    case NRF_DRV_POWER_USB_EVT_DETECTED:
        NRF_LOG_INFO("USB power detected");
        if (!nrf_drv_usbd_is_enabled()) {
            nrf_drv_usbd_enable();
        }
        break;
    case NRF_DRV_POWER_USB_EVT_REMOVED:
        NRF_LOG_INFO("USB power removed");
        m_usbd_configured = false;
        m_send_mouse_position = false;
        if (nrf_drv_usbd_is_started()) {
            nrf_drv_usbd_stop();
        }
        if (nrf_drv_usbd_is_enabled()) {
            nrf_drv_usbd_disable();
        }
        /*
         * Turn OFF LEDs
         */
        bsp_board_led_off(LED_USB_STATUS);
        break;
    case NRF_DRV_POWER_USB_EVT_READY:
        NRF_LOG_INFO("USB ready");
        if (!nrf_drv_usbd_is_started()) {
            nrf_drv_usbd_start(true);
        }
        break;
    default:
        ASSERT(false);
    }
}

void usb_init_power_clock(void)
{
    ret_code_t ret;
    /*
     * Initializing power and clock
     */
    ret = nrf_drv_clock_init();
    APP_ERROR_CHECK(ret);
    ret = nrf_drv_power_init(NULL);
    APP_ERROR_CHECK(ret);
    nrf_drv_clock_hfclk_request(NULL);
    nrf_drv_clock_lfclk_request(NULL);
    while (!(nrf_drv_clock_hfclk_is_running() &&
         nrf_drv_clock_lfclk_is_running())) {
        /*
         * Just waiting
         */
    }

    // ret = app_timer_init();
    // APP_ERROR_CHECK(ret);

    /*
     * Avoid warnings if assertion is disabled
     */
    UNUSED_VARIABLE(ret);
}

static void log_resetreason(void)
{
    /*
     * Reset reason
     */
    uint32_t rr = nrf_power_resetreas_get();
    NRF_LOG_INFO("Reset reasons:");
    if (0 == rr) {
        NRF_LOG_INFO("- NONE");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_RESETPIN_MASK)) {
        NRF_LOG_INFO("- RESETPIN");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_DOG_MASK)) {
        NRF_LOG_INFO("- DOG");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_SREQ_MASK)) {
        NRF_LOG_INFO("- SREQ");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_LOCKUP_MASK)) {
        NRF_LOG_INFO("- LOCKUP");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_OFF_MASK)) {
        NRF_LOG_INFO("- OFF");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_LPCOMP_MASK)) {
        NRF_LOG_INFO("- LPCOMP");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_DIF_MASK)) {
        NRF_LOG_INFO("- DIF");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_NFC_MASK)) {
        NRF_LOG_INFO("- NFC");
    }
    if (0 != (rr & NRF_POWER_RESETREAS_VBUS_MASK)) {
        NRF_LOG_INFO("- VBUS");
    }
}

void usb_setup_nrf(void) {
    ret_code_t ret;

    if (NRF_DRV_USBD_ERRATA_ENABLE) {
        NRF_LOG_INFO("USB errata 104 %s",
                 (uint32_t) (nrf_drv_usbd_errata_104()? "enabled" :
                     "disabled"));
        NRF_LOG_INFO("USB errata 154 %s",
                 (uint32_t) (nrf_drv_usbd_errata_154()? "enabled" :
                     "disabled"));
    }

    // Start USB
    ret = nrf_drv_usbd_init(usbd_event_handler);
    APP_ERROR_CHECK(ret);

    // Configure selected size of the packed on EP0
    nrf_drv_usbd_ep_max_packet_size_set(NRF_DRV_USBD_EPOUT0,
                        EP0_MAXPACKETSIZE);
    nrf_drv_usbd_ep_max_packet_size_set(NRF_DRV_USBD_EPIN0,
                        EP0_MAXPACKETSIZE);


    if (USBD_POWER_DETECTION) {
        static const nrf_drv_power_usbevt_config_t config = {
            .handler = power_usb_event_handler
        };
        ret = nrf_drv_power_usbevt_init(&config);
        APP_ERROR_CHECK(ret);
    } else {
        NRF_LOG_INFO
            ("No USB power detection enabled\r\nStarting USB now");
        nrf_delay_us(STARTUP_DELAY);
        if (!nrf_drv_usbd_is_enabled()) {
            nrf_drv_usbd_enable();
            ret = ep_configuration(0);
            APP_ERROR_CHECK(ret);
        }
        /*
         * Wait for regulator power up
         */
        while (NRF_DRV_POWER_USB_STATE_CONNECTED
               == nrf_drv_power_usbstatus_get()) {
            /*
             * Just waiting
             */
        }

        if (NRF_DRV_POWER_USB_STATE_READY ==
            nrf_drv_power_usbstatus_get()) {
            if (!nrf_drv_usbd_is_started()) {
                nrf_drv_usbd_start(true);
            }
        } else {
            nrf_drv_usbd_disable();
        }
    }

}

bool is_usb_configured(void) {
    return m_usbd_configured;
}

bool is_usb_suspended(void) {
    return m_usbd_suspended;
}

bool is_usb_remote_wakeup_enabled(void) {
    return m_usbd_rwu_enabled;
}


int usb_test(void)
{
    UNUSED_RETURN_VALUE(NRF_LOG_INIT(NULL));
    NRF_LOG_DEFAULT_BACKENDS_INIT();

    usb_init_power_clock();

    timer_init();

    log_resetreason();

    NRF_LOG_INFO("USDB example started.");

    usb_setup_nrf();

    nrf_power_resetreas_clear(nrf_power_resetreas_get());

    while (true) {
        uint32_t time = timer_read_ms();

        if ((time / 1000) % 2 == 0) {
            if (m_send_flag == 1) {
                m_mouse_dir = !m_mouse_dir;
            }

            m_send_flag = 0;
        } else {
            m_send_flag = 1;
        }

        if (m_usbd_configured) {
            if (m_send_flag) {
                if (m_usbd_suspended) {
                    if (m_usbd_rwu_enabled) {
                        UNUSED_RETURN_VALUE
                            (nrf_drv_usbd_wakeup_req());
                    }
                } else {
                    move_mouse_pointer();
                }
            }
        }

        UNUSED_RETURN_VALUE(NRF_LOG_PROCESS());

        // Even if we miss an event enabling USB, USB event would wake us up.
        __WFE();
        // Clear SEV flag if CPU was woken up by event
        __SEV();
        __WFE();
    }
}
