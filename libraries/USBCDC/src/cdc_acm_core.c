/*!
    \file  cdc_acm_core.c
    \brief CDC ACM driver

    \version 2020-01-25, V1.0.0, USB to UART bridge for GD32VF103
*/

/*
    Copyright (c) 2019, GigaDevice Semiconductor Inc.
    Copyright (c) 2020, LoBo

    Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this 
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice, 
       this list of conditions and the following disclaimer in the documentation 
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors 
       may be used to endorse or promote products derived from this software without 
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. 
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, 
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT 
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR 
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, 
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cdc_acm_core.h"

#define USBD_VID                          0x28e9
#define USBD_PID                          0x018a

static uint32_t cdc_cmd = 0xFFU;
static uint8_t usb_cmd_buffer[CDC_ACM_CMD_PACKET_SIZE];
uint8_t cdc_acm_rts = 1;
uint8_t cdc_acm_dtr = 1;
uint8_t cdc_acm_brk = 0;

uint32_t cdc_acm_receive_count = 0;
uint32_t cdc_acm_send_count = 0;
__IO uint8_t cdc_acm_packet_sent = 1U;
__IO uint8_t cdc_acm_packet_receive = 1U;
__IO uint32_t cdc_acm_receive_length = 0U;

//usbd_int_cb_struct *usbd_int_fops = NULL;

line_coding_struct linecoding =
{
    115200, /* baud rate     */
    0x00,   /* stop bits - 1 */
    0x00,   /* parity - none */
    0x08    /* num of bits 8 */
};

/* note:it should use the C99 standard when compiling the below codes */
/* USB standard device descriptor */
const usb_desc_dev device_descriptor =
{
    .header = 
     {
         .bLength = USB_DEV_DESC_LEN, 
         .bDescriptorType = USB_DESCTYPE_DEV
     },
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x02,  // Communications and CDC Control
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = USB_FS_EP0_MAX_LEN,
    .idVendor = USBD_VID,
    .idProduct = USBD_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = STR_IDX_MFC,
    .iProduct = STR_IDX_PRODUCT,
    .iSerialNumber = STR_IDX_SERIAL,
    .bNumberConfigurations = USBD_CFG_MAX_NUM
};

/* USB device configuration descriptor */
usb_descriptor_configuration_set_struct configuration_descriptor = 
{
    .config = 
    {
        .header = 
         {
            .bLength = USB_CFG_DESC_LEN,
            .bDescriptorType = USB_DESCTYPE_CONFIG
         },
        .wTotalLength = USB_CDC_ACM_CONFIG_DESC_SIZE,
        .bNumInterfaces = 0x02,
        .bConfigurationValue = 0x01,
        .iConfiguration = 0x00,
        .bmAttributes = 0x80,
        .bMaxPower = 0x32
    },

    .cdc_loopback_interface = 
    {
        .header = 
         {
             .bLength = USB_ITF_DESC_LEN,
             .bDescriptorType = USB_DESCTYPE_ITF 
         },
        .bInterfaceNumber = 0x00,
        .bAlternateSetting = 0x00,
        .bNumEndpoints = 0x01,
        .bInterfaceClass = 0x02,     // Communications Interface Class
        .bInterfaceSubClass = 0x02,  // Abstract Control Model
        .bInterfaceProtocol = 0x00,  // No class specific protocol required
        .iInterface = 0x00
    },

    .cdc_loopback_header = 
    {
        .header =
         {
            .bLength = sizeof(usb_descriptor_header_function_struct), 
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype = 0x00,
        .bcdCDC = 0x0110
    },

    .cdc_loopback_call_managment = 
    {
        .header = 
         {
            .bLength = sizeof(usb_descriptor_call_managment_function_struct), 
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype = 0x01,
        .bmCapabilities = 0x00,
        .bDataInterface = 0x01
    },

    .cdc_loopback_acm = 
    {
        .header = 
         {
            .bLength = sizeof(usb_descriptor_acm_function_struct), 
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype = 0x02,
        .bmCapabilities = 0x02,
    },

    .cdc_loopback_union = 
    {
        .header = 
         {
            .bLength = sizeof(usb_descriptor_union_function_struct), 
            .bDescriptorType = USB_DESCTYPE_CS_INTERFACE
         },
        .bDescriptorSubtype = 0x06,
        .bMasterInterface = 0x00,
        .bSlaveInterface0 = 0x01,
    },

    .cdc_loopback_cmd_endpoint = 
    {
        .header = 
         {
            .bLength = USB_EP_DESC_LEN, 
            .bDescriptorType = USB_DESCTYPE_EP
         },
        .bEndpointAddress = CDC_ACM_CMD_EP,
        .bmAttributes = 0x03,
        .wMaxPacketSize = CDC_ACM_CMD_PACKET_SIZE,
        .bInterval = 0x0A
    },

    .cdc_loopback_data_interface = 
    {
        .header = 
         {
            .bLength = USB_ITF_DESC_LEN,
            .bDescriptorType = USB_DESCTYPE_ITF
         },
        .bInterfaceNumber = 0x01,
        .bAlternateSetting = 0x00,
        .bNumEndpoints = 0x02,
        .bInterfaceClass = 0x0A,     // Data Interface Class
        .bInterfaceSubClass = 0x00,  // unused
        .bInterfaceProtocol = 0x00,  // No class specific protocol required
        .iInterface = 0x00
    },

    .cdc_loopback_out_endpoint = 
    {
        .header = 
         {
             .bLength = USB_EP_DESC_LEN, 
             .bDescriptorType = USB_DESCTYPE_EP 
         },
        .bEndpointAddress = CDC_ACM_DATA_OUT_EP,
        .bmAttributes = 0x02,
        .wMaxPacketSize = CDC_ACM_DATA_PACKET_SIZE,
        .bInterval = 0x00
    },

    .cdc_loopback_in_endpoint = 
    {
        .header = 
         {
             .bLength = USB_EP_DESC_LEN, 
             .bDescriptorType = USB_DESCTYPE_EP 
         },
        .bEndpointAddress = CDC_ACM_DATA_IN_EP,
        .bmAttributes = 0x02,
        .wMaxPacketSize = CDC_ACM_DATA_PACKET_SIZE,
        .bInterval = 0x00
    }
};

/* USB language ID Descriptor */
const usb_desc_LANGID usbd_language_id_desc = 
{
    .header = 
     {
         .bLength = sizeof(usb_desc_LANGID), 
         .bDescriptorType = USB_DESCTYPE_STR
     },
    .wLANGID = ENG_LANGID
};

void* const usbd_strings[] = 
{
    [STR_IDX_LANGID] = (uint8_t *)&usbd_language_id_desc,
    [STR_IDX_MFC] = USBD_STRING_DESC("GigaDevice"),
    [STR_IDX_PRODUCT] = USBD_STRING_DESC("GD32VF USB CDC ACM in FS Mode"),
    [STR_IDX_SERIAL] = USBD_STRING_DESC("GD32VF-3.0.0-012020")
};

/*!
    \brief      initialize the CDC ACM device
    \param[in]  pudev: pointer to USB device instance
    \param[in]  config_index: configuration index
    \param[out] none
    \retval     USB device operation status
*/
uint8_t cdc_acm_init (usb_dev *pudev, uint8_t config_index)
{
    /* initialize the data Tx/Rx endpoint */
    usbd_ep_setup(pudev, &(configuration_descriptor.cdc_loopback_in_endpoint));
    usbd_ep_setup(pudev, &(configuration_descriptor.cdc_loopback_out_endpoint));

    /* initialize the command Tx endpoint */
    usbd_ep_setup(pudev, &(configuration_descriptor.cdc_loopback_cmd_endpoint));

    return USBD_OK;
}

/*!
    \brief      de-initialize the CDC ACM device
    \param[in]  pudev: pointer to USB device instance
    \param[in]  config_index: configuration index
    \param[out] none
    \retval     USB device operation status
*/
uint8_t cdc_acm_deinit (usb_dev *pudev, uint8_t config_index)
{
    /* deinitialize the data Tx/Rx endpoint */
    usbd_ep_clear(pudev, CDC_ACM_DATA_IN_EP);
    usbd_ep_clear(pudev, CDC_ACM_DATA_OUT_EP);

    /* deinitialize the command Tx endpoint */
    usbd_ep_clear(pudev, CDC_ACM_CMD_EP);

    return USBD_OK;
}

/*!
    \brief      handle CDC ACM data
    \param[in]  pudev: pointer to USB device instance
    \param[in]  rx_tx: data transfer direction:
      \arg        USBD_TX
      \arg        USBD_RX
    \param[in]  ep_id: endpoint identifier
    \param[out] none
    \retval     USB device operation status
*/
uint8_t cdc_acm_data_out_handler (usb_dev *pudev, uint8_t ep_id)
{
    if ((EP0_OUT & 0x7F) == ep_id) 
    {
        cdc_acm_EP0_RxReady (pudev);
    } 
    else if ((CDC_ACM_DATA_OUT_EP & 0x7F) == ep_id) 
    {
        cdc_acm_packet_receive = 1;
        cdc_acm_receive_length = usbd_rxcount_get(pudev, CDC_ACM_DATA_OUT_EP);
        cdc_acm_receive_count += cdc_acm_receive_length;
        return USBD_OK;
    }
    return USBD_FAIL;
}

uint8_t cdc_acm_data_in_handler (usb_dev *pudev, uint8_t ep_id)
{
    if ((CDC_ACM_DATA_IN_EP & 0x7F) == ep_id) 
    {
        usb_transc *transc = &pudev->dev.transc_in[EP_ID(ep_id)];

        if ((transc->xfer_len % transc->max_len == 0) && (transc->xfer_len != 0)) {
            usbd_ep_send (pudev, ep_id, NULL, 0U);
        } else {
            cdc_acm_packet_sent = 1;
        }
        return USBD_OK;
    } 
    return USBD_FAIL;
}

//-------------------------------------------------
void cdc_acm_u8tocmdbuf(uint8_t idx, uint8_t data)
{
    usb_cmd_buffer[idx] = (uint8_t)(data);
}

void cdc_acm_u32tocmdbuf(uint8_t idx, uint32_t data)
{
    usb_cmd_buffer[idx] = (uint8_t)(data);
    usb_cmd_buffer[idx+1] = (uint8_t)(data >> 8);
    usb_cmd_buffer[idx+2] = (uint8_t)(data >> 16);
    usb_cmd_buffer[idx+3] = (uint8_t)(data >> 24);
}

uint8_t* cdc_acm_get_cmdbuf(void)
{
    return usb_cmd_buffer;
}

/*!
    \brief      handle the CDC ACM class-specific requests
    \param[in]  pudev: pointer to USB device instance
    \param[in]  req: device class-specific request
    \param[out] none
    \retval     USB device operation status
*/
uint8_t cdc_acm_req_handler (usb_dev *pudev, usb_req *req)
{
    switch (req->bRequest) 
    {
        case SEND_ENCAPSULATED_COMMAND:
            break;
        case GET_ENCAPSULATED_RESPONSE:
            break;
        case SET_COMM_FEATURE:
            break;
        case GET_COMM_FEATURE:
            break;
        case CLEAR_COMM_FEATURE:
            break;
        case SET_LINE_CODING:
            /* set the value of the current command to be processed */
            cdc_cmd = req->bRequest;
            /* enable EP0 prepare to receive command data packet */
            pudev->dev.transc_out[0].xfer_buf = usb_cmd_buffer;
            pudev->dev.transc_out[0].remain_len = req->wLength;
            break;
        case GET_LINE_CODING:
            cdc_acm_u32tocmdbuf(0, linecoding.dwDTERate);
            usb_cmd_buffer[4] = linecoding.bCharFormat;
            usb_cmd_buffer[5] = linecoding.bParityType;
            usb_cmd_buffer[6] = linecoding.bDataBits;
            /* send the request data to the host */
            pudev->dev.transc_in[0].xfer_buf = usb_cmd_buffer;
            pudev->dev.transc_in[0].remain_len = req->wLength;
            break;
        case SET_CONTROL_LINE_STATE:
            cdc_acm_rts = (req->wValue >> 1) & 1;
            cdc_acm_dtr = req->wValue & 1;
            cdc_acm_set_control_line_state(cdc_acm_rts, cdc_acm_dtr);
            break;
        case SEND_BREAK:
            cdc_acm_brk = cdc_acm_send_break(req->wValue != 0);
            break;
        default:
            return cdc_acm_custom_req_handler(pudev, req);
            break;
    }

    return USBD_OK;
}

/*!
    \brief      command data received on control endpoint
    \param[in]  pudev: pointer to USB device instance
    \param[out] none
    \retval     USB device operation status
*/
uint8_t cdc_acm_EP0_RxReady (usb_dev *pudev)
{
    if (cdc_cmd == SET_LINE_CODING) {
        /* process the command data */
        linecoding.dwDTERate = (uint32_t)(usb_cmd_buffer[0] | 
                                         (usb_cmd_buffer[1] << 8) |
                                         (usb_cmd_buffer[2] << 16) |
                                         (usb_cmd_buffer[3] << 24));
        linecoding.bCharFormat = usb_cmd_buffer[4] & 3;
        linecoding.bParityType = usb_cmd_buffer[5];
        linecoding.bDataBits = usb_cmd_buffer[6];
        cdc_acm_set_line_coding(&linecoding);

        cdc_cmd = NO_CMD;
    }

    return USBD_OK;
}


usb_class_core usbd_cdc_cb = {
    .command         = NO_CMD,
    .alter_set       = 0,

    .init            = cdc_acm_init,
    .deinit          = cdc_acm_deinit,
    .req_proc        = cdc_acm_req_handler,
    .data_in         = cdc_acm_data_in_handler,
    .data_out        = cdc_acm_data_out_handler
};
