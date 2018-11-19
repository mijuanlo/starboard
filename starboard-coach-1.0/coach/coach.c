/*
 * Zoran COACH 10P USB webcam module version 0.3
 *
 * Copyright (c) 2010 by: <cctsai@nutsfactory.net>
 *
 * Some video buffer code based on vivi.c drivers.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the BSD Licence, GNU General Public License
 * as published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version
 */
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/random.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/videodev2.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/freezer.h>
#include <media/videobuf-vmalloc.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-common.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
#include <media/v4l2-ioctl.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#include <linux/vmalloc.h>
#endif

#define COACH_MODULE_NAME "coach10p"

#define V4L2_CID_SENSORFLIP                     (V4L2_CID_PRIVATE_BASE+0)
#define V4L2_CID_ZOOMIN                         (V4L2_CID_PRIVATE_BASE+1)
#define V4L2_CID_ZOOMOUT                        (V4L2_CID_PRIVATE_BASE+2)

#define PRMID_STREAM_RATE			0x2001
#define PRMID_STREAM_CR				0x2002
#define PRMID_REQ_STREAM			0x2003
#define PRMID_REQ_SNAPSHOT			0x2004
#define PRMID_REQ_PREVIEW			0x2005
#define PRMID_SNAPSHOT_COMPLETE		        0x2007
#define PRMID_STREAM_WIDTH			0x200C
#define PRMID_STREAM_HEIGHT			0x200D
#define PRMID_SENSOR_FLIP			0x2006
#define PRMID_BRIGHTNESS			0x2008
#define PRMID_DC_EFFECT				0x2009
#define PRMID_POWER_OFF				0x200A
#define PRMID_HCE_MODE				0x200B
#define PRMID_HCE_VERSION			0x200E
#define PRMID_CUSTOM_ID				0x200F
#define PRMID_VK_ZOOM_IN			0x2176
#define PRMID_VK_ZOOM_OUT			0x2177
#define PRMID_VK_AUTO_FOCUS			0x2146

#define MAX_REPEAT_TIME		20000000		// 2s / 100ns
#define MIN_REPEAT_TIME		333333			// 1/30s / 100ns

typedef struct _STREAM_FORMAT {
    __u32	width;
    __u32	height;
    __u32	compressionratio;
    __u32	minframeinterval;
    __u32	maxframeinterval;
} STREAM_FORMAT;

STREAM_FORMAT gSupportedFormats[] = {
    { 320, 240, 12, 333333, MAX_REPEAT_TIME },
    { 640, 480, 12, 333333, MAX_REPEAT_TIME },
    { 960, 720, 16, 333333, MAX_REPEAT_TIME },
    { 1024,576, 16, 333333, MAX_REPEAT_TIME },  // 30fps
    { 1024,768, 16, 400000, MAX_REPEAT_TIME },  // 25fps
    { 1280,720, 16, 666667, MAX_REPEAT_TIME },  // 15fps
    { 1280,960, 16, 833333, MAX_REPEAT_TIME },  // 12fps
};

const int gNumSupportedFormat = ARRAY_SIZE(gSupportedFormats);

unsigned char JFIF_HEADER[636] = {
    // offset:0,size:2
    // <SOI,2>, Start of Image
    0xFF, 0xD8,

    // offset:2,size:134
    // <DQT,134>, Difine Quantization Table
    0xFF, 0xDB, 0x00, 0x84, 0x00, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x01, 0x01, 0x01, 0x02,
    0x02, 0x02, 0x02, 0x02, 0x04, 0x03, 0x02, 0x02, 0x02, 0x02, 0x05, 0x04, 0x04, 0x03, 0x04, 0x06,
    0x05, 0x06, 0x06, 0x06, 0x05, 0x06, 0x06, 0x06, 0x07, 0x09, 0x08, 0x06, 0x07, 0x09, 0x07, 0x06,
    0x06, 0x08, 0x0B, 0x08, 0x09, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x06, 0x08, 0x0B, 0x0C, 0x0B, 0x0A,
    0x0C, 0x09, 0x0A, 0x0A, 0x0A, 0x01, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x05, 0x03, 0x03, 0x05,
    0x0A, 0x07, 0x06, 0x07, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
    0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A,
#define OFFSET_OF_QTABLE_0		(2+5)
#define OFFSET_OF_QTABLE_1		(2+5+64+1)

#if 1
    // offset:2,size:18
    // <APP0,18>["JFIF", 1.02]
    0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46, 0x00, 0x01, 0x02, 0x00, 0x00, 0x01, 0x00, 0x01,
    0x00, 0x00,
#else
    // offset:2,size:18
    // <APP0,18>["AVI1", 1.02], Marker
    0xFF, 0xE0, 0x00, 0x10, 0x41, 0x56, 0x49, 0x31, 0x00, 0x01, 0x02, 0x00, 0x00, 0x01, 0x00, 0x01,
    0x00, 0x00,
#endif

    // offset:20,size:10
    // <APP0,10>["COACH"]
    0xFF, 0xE0, 0x00, 0x08, 0x43, 0x4F, 0x41, 0x43, 0x48, 0x00,

    // offset:164,size:6
    // <DRI,6>
    0xFF, 0xDD, 0x00, 0x04, 0x00, 0x00,

    // offset:170,size:33
    // <DHT,33>[Tc=0,Th=0 (DC)]
    0xFF, 0xC4, 0x00, 0x1F, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B,

    // offset:203,size:183
    // <DHT,183>[Tc=1,Th=0 (AC)]
    0xFF, 0xC4, 0x00, 0xB5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04,
    0x04, 0x00, 0x00, 0x01, 0x7D, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41,
    0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08, 0x23, 0x42, 0xB1,
    0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19,
    0x1A, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63, 0x64,
    0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84,
    0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2,
    0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8, 0xB9,
    0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7,
    0xD8, 0xD9, 0xDA, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3,
    0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,

    // offset:386,size:33
    // <DHT,33>[Tc=0,Th=1 (DC)]
    0xFF, 0xC4, 0x00, 0x1F, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
    0x0B,

    // offset:419,size:183
    // <DHT,183>[Tc=1,Th=1 (AC)]
    0xFF, 0xC4, 0x00, 0xB5, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05, 0x04,
    0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12,
    0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xA1, 0xB1, 0xC1,
    0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1, 0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1,
    0x17, 0x18, 0x19, 0x1A, 0x26, 0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43,
    0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5A, 0x63,
    0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82,
    0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
    0x9A, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7,
    0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5,
    0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3,
    0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA,

    // offset:602,size:20
    // <SOF0,20>
    0xFF, 0xC0, 0x00, 0x11, 0x08, 0x00, 0xF0, 0x01, 0x40, 0x03, 0x01, 0x21, 0x00, 0x02, 0x11, 0x01,
    0x03, 0x11, 0x01, 0xFF,
#define OFFSET_OF_FRAME_HEIGHT		(602+5)
#define OFFSET_OF_FRAME_WIDTH		(602+7)

    // offset:622,size:14
    // <SOS,14>
    0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3F, 0x00,

    // offset:636
#define LENGTH_OF_JFIF_HEADER		636
};

/* Camera */
#define FRAMES 1
#define MAX_FRAME_SIZE 200000
#define BUFFER_SIZE 0x1000
#define CTRL_TIMEOUT 500

#define ZR364XX_DEF_BUFS	4
#define ZR364XX_READ_IDLE	0
#define ZR364XX_READ_FRAME	1

/* Wake up at about 30 fps */
#define WAKE_NUMERATOR 30
#define WAKE_DENOMINATOR 1001
#define BUFFER_TIMEOUT msecs_to_jiffies(500)  /* 0.5 seconds */

#define COACH_MAJOR_VERSION 0
#define COACH_MINOR_VERSION 1
#define COACH_RELEASE 0
#define COACH_VERSION \
	KERNEL_VERSION(COACH_MAJOR_VERSION, COACH_MINOR_VERSION, COACH_RELEASE)

MODULE_DESCRIPTION("Zoran DCHD-5M based on COACH 10P");
MODULE_AUTHOR("Rex Tsai");
MODULE_LICENSE("Dual BSD/GPL");

static unsigned debug = 0;
module_param(debug, uint, 0644);
MODULE_PARM_DESC(debug, "activates debug info");

/* Debug macro */
#define DBG(fmt, args...) \
	do { \
		if (debug) { \
			printk(KERN_INFO KBUILD_MODNAME " " fmt, ##args); \
		} \
	} while (0)

#define FULL_DEBUG 1
#ifdef FULL_DEBUG
#define _DBG DBG
#else
#define _DBG(fmt, args...)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31) 
#define dprintk(dev, level, fmt, arg...) \
	v4l2_dbg(level, debug, dev->vfd, fmt, ## arg)
#else
#define dprintk(dev, level, fmt, arg...) \
	do { \
	    if (debug >= (level)) { \
	        printk(KERN_DEBUG KBUILD_MODNAME " " fmt, ##arg); \
	    } \
	} while (0)
#endif

/* Devices supported by this driver
 * .driver_info contains the init method used by the camera */
static struct usb_device_id device_table[] = {
    {USB_DEVICE(0x172f, 0x0080)},
    {} /* Terminating entry */
};

/* supported controls */
static struct v4l2_queryctrl coach_qctrl[] = {
    {
        .id            = V4L2_CID_BRIGHTNESS,
        .type          = V4L2_CTRL_TYPE_INTEGER,
        .name          = "Brightness",
        .minimum       = 0,
        .maximum       = 15,
        .step          = 1,
        .default_value = 7,
        .flags         = V4L2_CTRL_FLAG_SLIDER,
    },
    {
        .id             = V4L2_CID_FOCUS_AUTO,
        .type           = V4L2_CTRL_TYPE_BOOLEAN,
        .name           = "Focus, Auto",
        .minimum        = 0,
        .maximum        = 1,
        .step           = 1,
        .default_value  = 0,
    },
    {
        .id             = V4L2_CID_SENSORFLIP,
        .type           = V4L2_CTRL_TYPE_BOOLEAN,
        .name           = "Sensor flip",
        .minimum        = 0,
        .maximum        = 1,
        .step           = 1,
        .default_value  = 0,
    },
    {
        .id             = V4L2_CID_ZOOMIN,
        .type           = V4L2_CTRL_TYPE_BOOLEAN,
        .name           = "Zoom IN",
        .minimum        = 0,
        .maximum        = 1,
        .step           = 1,
        .default_value  = 0,
    },
    {
        .id             = V4L2_CID_ZOOMOUT,
        .type           = V4L2_CTRL_TYPE_BOOLEAN,
        .name           = "Zoom OUT",
        .minimum        = 0,
        .maximum        = 1,
        .step           = 1,
        .default_value  = 0,
    },
};

/* ------------------------------------------------------------------
	Basic structures
   ------------------------------------------------------------------*/

struct coach_fmt {
	char  *name;
	u32   fourcc; /* v4l2 format id */
	int   depth;
};

/* flags */
#define ZORAN_FORMAT_COMPRESSED 1<<0
#define ZORAN_FORMAT_OVERLAY    1<<1
#define ZORAN_FORMAT_CAPTURE    1<<2
#define ZORAN_FORMAT_PLAYBACK   1<<3

static struct coach_fmt formats[] = {
    {
        .name = "JPG",
        .fourcc = V4L2_PIX_FMT_JPEG,
        .depth = 24
    },
    {
        .name = "Hardware-encoded Motion-JPEG",
        .fourcc = V4L2_PIX_FMT_MJPEG,
        .depth = 24
    }
};

/* buffer for one video frame */
struct coach_buffer {
    /* common v4l buffer stuff -- must be first */
    struct videobuf_buffer vb;
    const struct coach_fmt *fmt;
};

struct coach_dmaqueue {
    struct list_head active;
    struct coach_dev *cam;

    /* Counters to control fps rate */
    int                        frame;
    int                        ini_jiffies;
};

/* frame structure */
struct zr364xx_framei {
	unsigned long ulState;	/* ulState:ZR364XX_READ_IDLE,
					   ZR364XX_READ_FRAME */
	void *lpvbits;		/* image data */
	unsigned long cur_size;	/* current data copied to it */
};

/* image buffer structure */
struct coach_bufferi {
    unsigned long dwFrames;                    /* number of frames in buffer */
    struct zr364xx_framei frame[FRAMES];       /* array of FRAME structures */
};

struct coach_pipeinfo {
    u32 transfer_size;
    u8 *transfer_buffer;
    u32 state;
    void *stream_urb;
    void *cam;	/* back pointer to coach_dev struct */
    u32 err_count;
    u32 idx;
};

struct coach_dev {
    struct usb_device *udev;	/* save off the usb device pointer */
    struct usb_interface *interface;/* the interface for this device */

    spinlock_t slock;
    struct mutex mutex;
    struct mutex open_lock;

    int users;
    int removed;

    /* various device info */
    struct video_device *vfd;
    struct coach_dmaqueue vidq;

    /* usb */
    u8 read_endpoint;

    /* pipeline */
    int			        b_acquire;
    int			        last_frame;
    int			        cur_frame;
    unsigned long		frame_count;
    struct coach_pipeinfo	pipe[1];
    struct coach_bufferi	buffer;

    int nb;
    int skip;

    /* Input Number */
    int			   input;

    /* Control 'registers' */
    int 		       qctl_regs[ARRAY_SIZE(coach_qctrl)];

    /* video capture */
    struct coach_fmt           *fmt;
    __u32  width, height;
    struct videobuf_queue      vb_vidq;

    enum v4l2_buf_type         type;
};

static void coach_destroy(struct coach_dev *dev);
static void read_pipe_completion(struct urb *purb);

/* starts acquisition process */
static int zr364xx_start_acquire(struct coach_dev *cam)
{
    int j;

    DBG("start acquire\n");

    cam->last_frame = -1;
    cam->cur_frame = 0;
    for (j = 0; j < FRAMES; j++) {
        cam->buffer.frame[j].ulState = ZR364XX_READ_IDLE;
        cam->buffer.frame[j].cur_size = 0;
    }
    cam->b_acquire = 1;
    return 0;
}

static inline int zr364xx_stop_acquire(struct coach_dev *cam)
{
    cam->b_acquire = 0;
    return 0;
}

static struct coach_fmt *get_format(struct v4l2_format *f)
{
    struct coach_fmt *fmt;
    unsigned int k;

    for (k = 0; k < ARRAY_SIZE(formats); k++) {
        fmt = &formats[k];
        if (fmt->fourcc == f->fmt.pix.pixelformat)
            break;
    }

    if (k == ARRAY_SIZE(formats))
        return NULL;

    return &formats[k];
}

static int 
coach_set_param(struct usb_device *udev, uint16_t param, uint16_t value) 
{
    int status;
    uint16_t data[3];
    int size = 3 * sizeof(uint16_t);
    unsigned char *transfer_buffer;

    data[0] = param;
    data[1] = (uint16_t)value & 0xFFFF;
    data[2] = (uint16_t)value >> 4;

    transfer_buffer = kmalloc(size, GFP_KERNEL);
    if (!transfer_buffer) {
	printk(KERN_ERR KBUILD_MODNAME "%s - kmalloc(%d) failed\n", __func__, size);
        return -ENOMEM;
    }

    memcpy(transfer_buffer,data, size);

    status = usb_control_msg(udev,
            usb_sndctrlpipe(udev, 0),
                1, // request type
                USB_DIR_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE, // request type
                0x1200,  // 0x1200, I43HC_SET_PARAM_LONG
                0, // index
                transfer_buffer, size, CTRL_TIMEOUT);

    kfree(transfer_buffer);

    if (status < 0)
	printk(KERN_ERR KBUILD_MODNAME "Failed sending control message, error %d.\n", status);

    return status;
}

static int 
coach_get_param(struct usb_device *udev, uint16_t param, uint16_t *value) 
{
    int status;
    unsigned char *transfer_buffer;
    int size = 2 * sizeof(uint16_t);

    transfer_buffer = kmalloc(size, GFP_KERNEL);
    if (!transfer_buffer) {
        dev_err(&udev->dev, "%s - kmalloc(%d) failed\n", __func__, size);
        return -ENOMEM;
    }

    status = usb_control_msg(udev,
            usb_rcvctrlpipe(udev, 0),
            1, // request type
            USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE, // request type
            0x0300,
            param,
            (void *)transfer_buffer, size, CTRL_TIMEOUT);

    if (status < 0)
        dev_err(&udev->dev, "Failed sending control message, error %d.\n", status);
    else 
        memcpy(value, transfer_buffer, size);

    kfree(transfer_buffer);
    return status;
}

static int zr364xx_start_readpipe(struct coach_dev *cam)
{
    int pipe;
    int retval;
    struct coach_pipeinfo *pipe_info = cam->pipe;

    if(cam->removed)
        return -EINVAL;

    if(coach_set_param(cam->udev, PRMID_REQ_STREAM, 1) < 0) {
        dev_err(&cam->udev->dev, "Request stream failed\n");
        return -EINVAL;
    }
    
    pipe = usb_rcvbulkpipe(cam->udev, cam->read_endpoint);
    DBG("%s: start pipe IN x%x\n", __func__, cam->read_endpoint);
    pipe_info->state = 1;
    pipe_info->err_count = 0;
    pipe_info->stream_urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!pipe_info->stream_urb) {
        dev_err(&cam->udev->dev, "ReadStream: Unable to alloc URB\n");
        return -ENOMEM;
    }

    /* transfer buffer allocated in board_init */
    usb_fill_bulk_urb(pipe_info->stream_urb, cam->udev,
            pipe,
            pipe_info->transfer_buffer,
            pipe_info->transfer_size,
            read_pipe_completion, pipe_info);

    DBG("submitting URB %p\n", pipe_info->stream_urb);
    retval = usb_submit_urb(pipe_info->stream_urb, GFP_KERNEL);
    if (retval) {
        printk(KERN_ERR KBUILD_MODNAME ": start read pipe failed\n");
        return retval;
    }

    return 0;
}

static void zr364xx_stop_readpipe(struct coach_dev *dev)
{
    struct coach_pipeinfo *pipe_info;

    if (dev == NULL) {
        printk(KERN_ERR KBUILD_MODNAME ": invalid device\n");
        return;
    }

    DBG("stop read pipe\n");

    pipe_info = dev->pipe;
    if (pipe_info) {
        if (pipe_info->state != 0)
            pipe_info->state = 0;

        if (pipe_info->stream_urb) {
            /* cancel urb */
            usb_kill_urb(pipe_info->stream_urb);
            usb_free_urb(pipe_info->stream_urb);
            pipe_info->stream_urb = NULL;
        }
    }
    coach_set_param(dev->udev, PRMID_REQ_STREAM, 0);
    return;
}


/* ------------------------------------------------------------------
	Videobuf operations
   ------------------------------------------------------------------*/
static int 
buffer_setup(struct videobuf_queue *vq, unsigned int *count, unsigned int *size)
{
    struct coach_dev  *dev = vq->priv_data;
    dprintk(dev, 1, "%s\n", __func__);

    *size = dev->width * dev->height * 2;
    if (0 == *count)
        *count = ZR364XX_DEF_BUFS;

    if (*size * *count > ZR364XX_DEF_BUFS * 1024 * 1024)
        *count = (ZR364XX_DEF_BUFS * 1024 * 1024) / *size;

    return 0;
}

static void free_buffer(struct videobuf_queue *vq, struct coach_buffer *buf)
{
    struct coach_dev *dev = vq->priv_data;

    dprintk(dev, 1, "%s, state: %i\n", __func__, buf->vb.state);

    if (in_interrupt())
        BUG();

    videobuf_vmalloc_free(&buf->vb);
    dprintk(dev, 1, "free_buffer: freed\n");
    buf->vb.state = VIDEOBUF_NEEDS_INIT;
}

#define norm_maxw() 1280
#define norm_maxh() 960
static int buffer_prepare(struct videobuf_queue *vq, struct videobuf_buffer *vb, enum v4l2_field field)
{
    struct coach_dev    *dev = vq->priv_data;
    struct coach_buffer *buf = container_of(vb, struct coach_buffer, vb);
    int rc;

    dprintk(dev, 1, "%s, field=%d\n", __func__, field);

    BUG_ON(NULL == dev->fmt);

    if (dev->width < 320 || dev->width  > norm_maxw() ||
            dev->height < 240 || dev->height > norm_maxh())
        return -EINVAL;

    buf->vb.size = dev->width * dev->height * 2;
    if (0 != buf->vb.baddr  &&  buf->vb.bsize < buf->vb.size)
        return -EINVAL;

    /* These properties only change when queue is idle, see s_fmt */
    buf->fmt       = dev->fmt;
    buf->vb.width  = dev->width;
    buf->vb.height = dev->height;
    buf->vb.field  = field;

    if (VIDEOBUF_NEEDS_INIT == buf->vb.state) {
        rc = videobuf_iolock(vq, &buf->vb, NULL);
        if (rc < 0)
            goto fail;
    }

    buf->vb.state = VIDEOBUF_PREPARED;

    return 0;

fail:
    free_buffer(vq, buf);
    return rc;
}

static void buffer_queue(struct videobuf_queue *vq, struct videobuf_buffer *vb)
{
    struct coach_buffer    *buf  = container_of(vb, struct coach_buffer, vb);
    struct coach_dev       *dev  = vq->priv_data;
    struct coach_dmaqueue *vidq = &dev->vidq;

    dprintk(dev, 1, "%s\n", __func__);

    buf->vb.state = VIDEOBUF_QUEUED;
    list_add_tail(&buf->vb.queue, &vidq->active);
}

static void buffer_release(struct videobuf_queue *vq,
			   struct videobuf_buffer *vb)
{
    struct coach_buffer   *buf  = container_of(vb, struct coach_buffer, vb);
    struct coach_dev      *dev  = vq->priv_data;

    dprintk(dev, 1, "%s\n", __func__);
    free_buffer(vq, buf);
}

static struct videobuf_queue_ops coach_video_qops = {
    .buf_setup      = buffer_setup,
    .buf_prepare    = buffer_prepare,
    .buf_queue      = buffer_queue,
    .buf_release    = buffer_release,
};

/* ------------------------------------------------------------------
	IOCTL vidioc handling
   ------------------------------------------------------------------*/
static int vidioc_querycap(struct file *file, void  *priv,
					struct v4l2_capability *cap)
{
    struct coach_dev *dev = priv;

    strcpy(cap->driver, "coach");
    strcpy(cap->card, "coach");
    strlcpy(cap->driver, COACH_MODULE_NAME, sizeof(cap->driver));
    strlcpy(cap->card, dev->udev->product, sizeof(cap->card));
    strlcpy(cap->bus_info, dev_name(&dev->udev->dev), sizeof(cap->bus_info));
    cap->version = COACH_VERSION;
    cap->capabilities =	V4L2_CAP_VIDEO_CAPTURE |
        V4L2_CAP_STREAMING |
        V4L2_CAP_READWRITE;
    return 0;
}

static int vidioc_enum_fmt_vid_cap(struct file *file, void  *priv,
        struct v4l2_fmtdesc *f)
{
    struct coach_fmt *fmt;

    if (f->index >= ARRAY_SIZE(formats))
        return -EINVAL;

    fmt = &formats[f->index];

    strlcpy(f->description, fmt->name, sizeof(f->description));
    f->pixelformat = fmt->fourcc;
    return 0;
}

static int vidioc_g_fmt_vid_cap(struct file *file, void *priv,
        struct v4l2_format *f)
{
    struct coach_dev *dev = priv;

    f->fmt.pix.width        = dev->width;
    f->fmt.pix.height       = dev->height;
    f->fmt.pix.field        = dev->vb_vidq.field;
    f->fmt.pix.pixelformat  = dev->fmt->fourcc;
    f->fmt.pix.bytesperline = (f->fmt.pix.width * dev->fmt->depth) >> 3;
    f->fmt.pix.sizeimage =
        f->fmt.pix.height * f->fmt.pix.bytesperline;

    return (0);
}

static char *decode_fourcc(__u32 pixelformat, char *buf)
{
    buf[0] = pixelformat & 0xff;
    buf[1] = (pixelformat >> 8) & 0xff;
    buf[2] = (pixelformat >> 16) & 0xff;
    buf[3] = (pixelformat >> 24) & 0xff;
    buf[4] = '\0';
    return buf;
}

static int vidioc_try_fmt_vid_cap(struct file *file, void *priv,
			struct v4l2_format *f)
{
    struct coach_dev *dev = priv;
    struct coach_fmt *fmt;
    char pixelformat_name[5];
    int i;

    dprintk(dev, 1, "%s\n", __func__);
    fmt = get_format(f);
    if (!fmt) {
        dprintk(dev, 1, "Fourcc format (0x%08x) invalid.\n", f->fmt.pix.pixelformat);
        return -EINVAL;
    }

    if (f->fmt.pix.pixelformat != V4L2_PIX_FMT_JPEG &&
            f->fmt.pix.pixelformat != V4L2_PIX_FMT_MJPEG) {
        dprintk(dev, 1, "%s: unsupported pixelformat V4L2_PIX_FMT_%s\n", __func__,
                decode_fourcc(f->fmt.pix.pixelformat, pixelformat_name));
        return -EINVAL;
    }

    if (f->fmt.pix.width <= 320 || f->fmt.pix.height <= 240) {
        f->fmt.pix.width = 320;
        f->fmt.pix.height = 240;
    }

    // search find closest mode.
    for (i = (gNumSupportedFormat - 1) ; i >= 0; i-- ) {
        if (f->fmt.pix.width >= gSupportedFormats[i].width && f->fmt.pix.height >= gSupportedFormats[i].height) {
            f->fmt.pix.width = gSupportedFormats[i].width;
            f->fmt.pix.height = gSupportedFormats[i].height;
            break;
        }
    }

    f->fmt.pix.field = V4L2_FIELD_NONE;
    f->fmt.pix.bytesperline = f->fmt.pix.width * 2;
    f->fmt.pix.sizeimage = f->fmt.pix.height * f->fmt.pix.bytesperline;
    f->fmt.pix.colorspace = 0;
    f->fmt.pix.priv = 0;
    DBG("%s: V4L2_PIX_FMT_%s (%d) ok!\n", __func__,
            decode_fourcc(f->fmt.pix.pixelformat, pixelformat_name),
            f->fmt.pix.field);
    return 0;
}

static int vidioc_s_fmt_vid_cap(struct file *file, void *priv,
					struct v4l2_format *f)
{
    struct coach_dev *dev = priv;
    struct videobuf_queue *q = &dev->vb_vidq;
    int ret;

    if(dev->removed)
        return -EINVAL;
 
    ret = vidioc_try_fmt_vid_cap(file, dev, f);
    if (ret < 0) {
        dprintk(dev, 1, "%s queue busy\n", __func__);
        return ret;
    }

    mutex_lock(&q->vb_lock);

    if (videobuf_queue_is_busy(&dev->vb_vidq)) {
        dprintk(dev, 1, "%s queue busy\n", __func__);
        ret = -EBUSY;
        goto out;
    }

    dev->fmt           = get_format(f);
    dev->width         = f->fmt.pix.width;
    dev->height        = f->fmt.pix.height;
    dev->vb_vidq.field = f->fmt.pix.field;
    dev->type          = f->type;

    zr364xx_stop_readpipe(dev);

    if(coach_set_param(dev->udev, PRMID_STREAM_WIDTH, dev->width) < 0) {
        dprintk(dev, 1, "set width failed");
        return -EINVAL;
    }
    if(coach_set_param(dev->udev, PRMID_STREAM_HEIGHT, dev->height) < 0) {
        dprintk(dev, 1, "set height fialed");
        return -EINVAL;
    }

    if( dev->width > 640 ||
            dev->height > 480 ) {
        coach_set_param(dev->udev, PRMID_STREAM_CR, 20);
    } else {
        coach_set_param(dev->udev, PRMID_STREAM_CR, 16);
    }

    // TODO, change frame rate.
    // wRate = (WORD) (10000000 / pvi->AvgTimePerFrame);
    zr364xx_start_readpipe(dev);

    /* Added some delay here, since opening/closing the camera quickly,
     * like Ekiga does during its startup, can crash the webcam
     */
    mdelay(100);
    dev->skip = 2;

    ret = 0;
out:
    mutex_unlock(&q->vb_lock);

    return ret;
}

static int vidioc_reqbufs(struct file *file, void *priv,
        struct v4l2_requestbuffers *p)
{
    struct coach_dev  *dev = priv;
    return (videobuf_reqbufs(&dev->vb_vidq, p));
}

static int vidioc_querybuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct coach_dev  *dev = priv;
    return (videobuf_querybuf(&dev->vb_vidq, p));
}

static int vidioc_qbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct coach_dev *dev = priv;
    return (videobuf_qbuf(&dev->vb_vidq, p));
}

static int vidioc_dqbuf(struct file *file, void *priv, struct v4l2_buffer *p)
{
    struct coach_dev  *dev = priv;
    return (videobuf_dqbuf(&dev->vb_vidq, p, file->f_flags & O_NONBLOCK));
}

#ifdef CONFIG_VIDEO_V4L1_COMPAT
static int vidiocgmbuf(struct file *file, void *priv, struct video_mbuf *mbuf)
{
    struct coach_dev  *dev = priv;
    return videobuf_cgmbuf(&dev->vb_vidq, mbuf, 8);
}
#endif

static int vidioc_streamon(struct file *file, void *priv, enum v4l2_buf_type i)
{
    struct coach_dev  *dev = priv;

    if (dev->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;

    if (i != dev->type)
        return -EINVAL;

    zr364xx_start_readpipe(dev);
    zr364xx_start_acquire(dev);
    return videobuf_streamon(&dev->vb_vidq);
}

static int vidioc_streamoff(struct file *file, void *priv, enum v4l2_buf_type i)
{
    struct coach_dev *dev = priv;

    if (dev->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
        return -EINVAL;
    if (i != dev->type)
        return -EINVAL;
    if(dev->removed)
        return -EINVAL;

    if (dev->b_acquire)
        zr364xx_stop_acquire(dev);

    zr364xx_stop_readpipe(dev);
    return videobuf_streamoff(&dev->vb_vidq);
}

static int vidioc_s_std(struct file *file, void *priv, v4l2_std_id i)
{
    return 0;
}

/* only one input in this sample driver */
static int vidioc_enum_input(struct file *file, void *priv,
				struct v4l2_input *inp)
{
    if (inp->index != 0)
        return -EINVAL;
    strcpy(inp->name, COACH_MODULE_NAME " camera");
    inp->type = V4L2_INPUT_TYPE_CAMERA;
    return (0);
}

static int vidioc_g_input(struct file *file, void *priv, unsigned int *i)
{
    struct coach_dev *dev = priv;

    *i = dev->input;

    return (0);
}

static int vidioc_s_input(struct file *file, void *priv, unsigned int i)
{
    if (i != 0)
        return -EINVAL;
    return (0);
}

/* --- controls ---------------------------------------------- */
static int vidioc_queryctrl(struct file *file, void *priv,
        struct v4l2_queryctrl *qc)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(coach_qctrl); i++)
        if (qc->id && qc->id == coach_qctrl[i].id) {
            memcpy(qc, &(coach_qctrl[i]), sizeof(*qc));
            return (0);
        }

    return -EINVAL;
}

static int vidioc_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *ctrl)
{
    struct coach_dev *dev = priv;
    int i;

    for (i = 0; i < ARRAY_SIZE(coach_qctrl); i++)
        if (ctrl->id == coach_qctrl[i].id) {
            ctrl->value = dev->qctl_regs[i];
            return 0;
        }

    return -EINVAL;
}

static int vidioc_s_ctrl(struct file *file, void *priv,
        struct v4l2_control *ctrl)
{
    struct coach_dev *dev = priv;
    int ret = 0;
    int i;

    if(dev->removed == 1)
	return -EINVAL;

    for (i = 0; i < ARRAY_SIZE(coach_qctrl); i++)
        if (ctrl->id == coach_qctrl[i].id) {
            if (ctrl->value < coach_qctrl[i].minimum ||
                    ctrl->value > coach_qctrl[i].maximum) {
                return -ERANGE;
            }
            dev->qctl_regs[i] = ctrl->value;

            mutex_lock(&dev->mutex);
            switch (ctrl->id) {
                case V4L2_CID_BRIGHTNESS:
                    dprintk(dev, 0, "%s changing brightness %d\n", __func__, ctrl->value);
                    ret = coach_set_param(dev->udev, PRMID_BRIGHTNESS, ctrl->value);
                    break;
                case V4L2_CID_FOCUS_AUTO:
                    dprintk(dev, 0, "%s autofocus %d\n", __func__, ctrl->value);
                    ret = coach_set_param(dev->udev, PRMID_VK_AUTO_FOCUS, ctrl->value);
                    break;
                case V4L2_CID_SENSORFLIP:
                    dprintk(dev, 0, "%s V4L2_CID_SENSORFLIP %d\n", __func__, ctrl->value);
                    ret = coach_set_param(dev->udev, PRMID_SENSOR_FLIP, ctrl->value);
                    break;
                case V4L2_CID_ZOOMIN:
                    dprintk(dev, 0, "%s V4L2_CID_ZOOMIN %d\n", __func__, ctrl->value);
                    ret = coach_set_param(dev->udev, PRMID_VK_ZOOM_IN, ctrl->value);
                    break;
                case V4L2_CID_ZOOMOUT:
                    dprintk(dev, 0, "%s V4L2_CID_ZOOMOUT %d\n", __func__, ctrl->value);
                    ret = coach_set_param(dev->udev, PRMID_VK_ZOOM_OUT, ctrl->value);
                    break;
            }
            mutex_unlock(&dev->mutex);

            return ret;
        }

    return -EINVAL;
}

/* ------------------------------------------------------------------
	File operations for the device
   ------------------------------------------------------------------*/

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
static int
coach_open (struct inode *inode, struct file  *file)
{
    struct video_device *vdev = video_devdata(file);
    struct coach_dev *dev = video_get_drvdata (vdev);
#else
static int coach_open(struct file *file)
{
    struct coach_dev *dev = video_drvdata(file);
#endif

    dprintk(dev, 1, "%s\n", __func__);
    mutex_lock(&dev->mutex);
    dev->users++;

    if (dev->users > 1) {
        dev->users--;
        mutex_unlock(&dev->mutex);
        return -EBUSY;
    }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
    dprintk(dev, 1, "open /dev/video%d type=%s users=%d\n", dev->vfd->num,
            v4l2_type_names[V4L2_BUF_TYPE_VIDEO_CAPTURE], dev->users);
#endif

    file->private_data = dev;
    dev->type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    dev->fmt      = &formats[0];
    dev->width    = 320;
    dev->height   = 240;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 38)
    videobuf_queue_vmalloc_init(&dev->vb_vidq, &coach_video_qops,
            NULL, &dev->slock, dev->type, V4L2_FIELD_INTERLACED,
            sizeof(struct coach_buffer), dev, NULL);
#else
    videobuf_queue_vmalloc_init(&dev->vb_vidq, &coach_video_qops,
            NULL, &dev->slock, dev->type, V4L2_FIELD_INTERLACED,
            sizeof(struct coach_buffer), dev);
#endif

    /* Added some delay here, since opening/closing the camera quickly,
     * like Ekiga does during its startup, can crash the webcam
     */
    mdelay(100);
    mutex_unlock(&dev->mutex);
    return 0;
}

static ssize_t
coach_read(struct file *file, char __user *data, size_t count, loff_t *ppos)
{
    struct coach_dev *dev = file->private_data;

    dprintk(dev, 1, "%s\n", __func__);
    if (!data)
        return -EINVAL;

    if (!count)
        return -EINVAL;

    if (dev->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        return videobuf_read_stream(&dev->vb_vidq, data, count, ppos, 0,
                file->f_flags & O_NONBLOCK);
    }
    return 0;
}

static unsigned int coach_poll(struct file *file, struct poll_table_struct *wait)
{
    struct coach_dev      *dev = file->private_data;
    struct videobuf_queue *q = &dev->vb_vidq;

    dprintk(dev, 1, "%s\n", __func__);

    if (V4L2_BUF_TYPE_VIDEO_CAPTURE != dev->type)
        return POLLERR;
    if(dev->removed)
        return POLLERR;

    return videobuf_poll_stream(file, q, wait);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 31)
static int coach_close(struct inode *inode, struct file *file) 
#else
static int coach_close(struct file *file)
#endif
{
    struct coach_dev *dev     = file->private_data;

    int minor = video_devdata(file)->minor;

    dprintk(dev, 1, "%s\n", __func__);

    videobuf_stop(&dev->vb_vidq);
    videobuf_mmap_free(&dev->vb_vidq);

    mutex_lock(&dev->mutex);
    dev->users--;
    mutex_unlock(&dev->mutex);

    dprintk(dev, 1, "close called (minor=%d, users=%d)\n",
		    minor, dev->users);

    if(dev->removed)
	coach_destroy(dev);

    return 0;
}

static int coach_mmap(struct file *file, struct vm_area_struct *vma)
{
    struct coach_dev *dev = file->private_data;
    int ret;

    dprintk(dev, 1, "%s mmap called, vma=0x%08lx\n", __func__, (unsigned long)vma);

    ret = videobuf_mmap_mapper(&dev->vb_vidq, vma);

    dprintk(dev, 1, "vma start=0x%08lx, size=%ld, ret=%d\n",
            (unsigned long)vma->vm_start,
            (unsigned long)vma->vm_end-(unsigned long)vma->vm_start,
            ret);

    return ret;
}

/* video buffer vmalloc implementation based partly on VIVI driver which is
 *          Copyright (c) 2006 by
 *                  Mauro Carvalho Chehab <mchehab--a.t--infradead.org>
 *                  Ted Walther <ted--a.t--enumera.com>
 *                  John Sokol <sokol--a.t--videotechnology.com>
 *                  http://v4l.videotechnology.com/
 *
 */
static void 
zr364xx_fillbuff(struct coach_dev *cam, struct coach_buffer *buf, int jpgsize)
{
    int pos = 0;
    struct timeval ts;
    const char *tmpbuf;
    char *vbuf = videobuf_to_vmalloc(&buf->vb);
    unsigned long last_frame;
    struct zr364xx_framei *frm;

    if (!vbuf)
        return;

    last_frame = cam->last_frame;
    if (last_frame != -1) {
        frm = &cam->buffer.frame[last_frame];
        tmpbuf = (const char *)cam->buffer.frame[last_frame].lpvbits;
        switch (buf->fmt->fourcc) {
            case V4L2_PIX_FMT_JPEG:
            case V4L2_PIX_FMT_MJPEG:
                buf->vb.size = jpgsize;
                memcpy(vbuf, tmpbuf, buf->vb.size);
                break;
            default:
                printk(KERN_DEBUG KBUILD_MODNAME ": unknown format?\n");
        }
        cam->last_frame = -1;
    } else {
        printk(KERN_ERR KBUILD_MODNAME ": ======= no frame\n");
        return;
    }
    DBG("%s: Buffer 0x%08lx size= %d\n", __func__, (unsigned long)vbuf, pos);
    /* tell v4l buffer was filled */

    buf->vb.field_count = cam->frame_count * 2;
    do_gettimeofday(&ts);
    buf->vb.ts = ts;
    buf->vb.state = VIDEOBUF_DONE;
}

static int 
zr364xx_got_frame(struct coach_dev *cam, int jpgsize)
{
    struct coach_dmaqueue *dma_q = &cam->vidq;
    struct coach_buffer *buf;
    unsigned long flags = 0;
    int rc = 0;

    DBG("wakeup: %p\n", &dma_q);

    spin_lock_irqsave(&cam->slock, flags);

    if (list_empty(&dma_q->active)) {
        DBG("No active queue to serve\n");
        rc = -1;
        goto unlock;
    }
    buf = list_entry(dma_q->active.next,
            struct coach_buffer, vb.queue);

    if (!waitqueue_active(&buf->vb.done)) {
        /* no one active */
        rc = -1;
        goto unlock;
    }
    list_del(&buf->vb.queue);

    do_gettimeofday(&buf->vb.ts);

    /* Fill buffer */
    zr364xx_fillbuff(cam, buf, jpgsize);
    DBG("filled buffer %p\n", buf);

    wake_up(&buf->vb.done);
    DBG("wakeup [buf/i] [%p/%d]\n", buf, buf->vb.i);
unlock:
    spin_unlock_irqrestore(&cam->slock, flags);
    return rc;
}

/* this function moves the usb stream read pipe data
 * into the system buffers.
 * returns 0 on success, EAGAIN if more data to process (call this
 * function again).
 *
 * Source: zr364xx.c
 */
static int zr364xx_read_video_callback(struct coach_dev *cam,
					struct coach_pipeinfo *pipe_info,
					struct urb *purb)
{
    unsigned char *pdest;
    unsigned char *psrc;
    unsigned char *ptr = NULL;
    struct zr364xx_framei *frm;
    s32 idx = -1;

    idx = cam->cur_frame;
    frm = &cam->buffer.frame[idx];

    /* search done.  now find out if should be acquiring */
    if (!cam->b_acquire) {
        /* we found a frame, but this channel is turned off */
        frm->ulState = ZR364XX_READ_IDLE;
        return -EINVAL;
    }

    psrc = (u8 *)pipe_info->transfer_buffer;
    ptr = pdest = frm->lpvbits;

    if (frm->ulState == ZR364XX_READ_IDLE) {
        frm->ulState = ZR364XX_READ_FRAME;
        frm->cur_size = 0;

        memcpy( &JFIF_HEADER[OFFSET_OF_QTABLE_0], psrc, 64 );
        memcpy( &JFIF_HEADER[OFFSET_OF_QTABLE_1], psrc + 64, 64 );

        JFIF_HEADER[OFFSET_OF_FRAME_HEIGHT+0]	= (cam->height >> 8) & 0xFF;
        JFIF_HEADER[OFFSET_OF_FRAME_HEIGHT+1]	= cam->height & 0xFF;
        JFIF_HEADER[OFFSET_OF_FRAME_WIDTH+0]	= (cam->width >> 8) & 0xFF;
        JFIF_HEADER[OFFSET_OF_FRAME_WIDTH+1]	= cam->width & 0xFF;

        memcpy( ptr, JFIF_HEADER, LENGTH_OF_JFIF_HEADER );

        ptr += LENGTH_OF_JFIF_HEADER;
        memcpy(ptr, psrc + 128, purb->actual_length - 128);
        ptr += purb->actual_length - 128;
        frm->cur_size = ptr - pdest;
    } else {
        if (frm->cur_size + purb->actual_length > MAX_FRAME_SIZE) {
            dev_info(&cam->udev->dev, "%s: buffer (%d bytes) too small to hold " "frame data. Discarding frame data.\n", __func__, MAX_FRAME_SIZE);
        } else {
            pdest += frm->cur_size;
            memcpy(pdest, psrc, purb->actual_length);
            frm->cur_size += purb->actual_length;
        }
    }

    if (purb->actual_length < pipe_info->transfer_size) {
        _DBG("****************Buffer[%d]full*************\n", idx);
        cam->last_frame = cam->cur_frame;
        cam->cur_frame++;
        /* end of system frame ring buffer, start at zero */
        if (cam->cur_frame == cam->buffer.dwFrames)
            cam->cur_frame = 0;

        /* frame ready */
        /* go back to find the JPEG EOI marker */
        ptr = pdest = frm->lpvbits;
        ptr += frm->cur_size - 2;
        while (ptr > pdest) {
            if (*ptr == 0xFF && *(ptr + 1) == 0xD9 && *(ptr + 2) == 0xFF)
                break;
            ptr--;
        }
        if (ptr == pdest)
            DBG("No EOI marker\n");

        /* Sometimes there is junk data in the middle of the picture,
         * we want to skip this bogus frames */
        while (ptr > pdest) {
            if (*ptr == 0xFF && *(ptr + 1) == 0xFF
                    && *(ptr + 2) == 0xFF)
                break;
            ptr--;
        }
        if (ptr != pdest) {
            DBG("Bogus frame ? %d\n", ++(cam->nb));
        } else if (cam->b_acquire) {
            /* we skip the 2 first frames which are usually buggy */
            if (cam->skip)
                cam->skip--;
            else {
                zr364xx_got_frame(cam, frm->cur_size);
            }
        }
        cam->frame_count++;
        frm->ulState = ZR364XX_READ_IDLE;
        frm->cur_size = 0;
    }
    /* done successfully */
    return 0;
}

static void
read_pipe_completion(struct urb *purb)
{
    struct coach_pipeinfo *pipe_info;
    struct coach_dev *cam;
    int pipe;

    pipe_info = purb->context;
    _DBG("%s %p, status %d\n", __func__, purb, purb->status);
    if (pipe_info == NULL) {
        printk(KERN_ERR KBUILD_MODNAME ": no context!\n");
        return;
    }

    cam = pipe_info->cam;
    if (cam == NULL) {
        printk(KERN_ERR KBUILD_MODNAME ": no context!\n");
        return;
    }

    /* if shutting down, do not resubmit, exit immediately */
    if (purb->status == -ESHUTDOWN) {
        DBG("%s, err shutdown\n", __func__);
        pipe_info->err_count++;
        return;
    }

    if (pipe_info->state == 0) {
        DBG("exiting USB pipe\n");
        return;
    }

    if (purb->actual_length < 0 ||
            purb->actual_length > pipe_info->transfer_size) {
        dev_err(&cam->udev->dev, "wrong number of bytes\n");
        return;
    }

    if (purb->status == 0) {
        zr364xx_read_video_callback(cam, pipe_info, purb);
    } else {
        pipe_info->err_count++;
        DBG("%s: failed URB %d\n", __func__, purb->status);
    }

    pipe = usb_rcvbulkpipe(cam->udev, cam->read_endpoint);

    /* reuse urb */
    usb_fill_bulk_urb(pipe_info->stream_urb, cam->udev,
            pipe,
            pipe_info->transfer_buffer,
            pipe_info->transfer_size,
            read_pipe_completion, pipe_info);

    if (pipe_info->state != 0) {
        purb->status = usb_submit_urb(pipe_info->stream_urb, GFP_ATOMIC);

        if (purb->status)
            dev_err(&cam->udev->dev, "error submitting urb (error=%i)\n", purb->status);
    } else {
        DBG("read pipe complete state 0\n");
    }
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
static const struct v4l2_file_operations coach_fops = {
#else
static const struct file_operations coach_fops = {
#endif
    .owner	    = THIS_MODULE,
    .open           = coach_open,
    .release        = coach_close,
    .read           = coach_read,
    .poll	    = coach_poll,
    .mmap           = coach_mmap,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
    .unlocked_ioctl = video_ioctl2, /* V4L2 ioctl handler */
#else
    .ioctl          = video_ioctl2, /* V4L2 ioctl handler */
#endif
};

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
static const struct v4l2_ioctl_ops coach_ioctl_ops = {
    .vidioc_querycap          = vidioc_querycap,
    .vidioc_enum_fmt_vid_cap  = vidioc_enum_fmt_vid_cap,
    .vidioc_g_fmt_vid_cap     = vidioc_g_fmt_vid_cap,
    .vidioc_try_fmt_vid_cap   = vidioc_try_fmt_vid_cap,
    .vidioc_s_fmt_vid_cap     = vidioc_s_fmt_vid_cap,
    .vidioc_reqbufs           = vidioc_reqbufs,
    .vidioc_querybuf          = vidioc_querybuf,
    .vidioc_qbuf              = vidioc_qbuf,
    .vidioc_dqbuf             = vidioc_dqbuf,
    .vidioc_s_std             = vidioc_s_std,
    .vidioc_enum_input        = vidioc_enum_input,
    .vidioc_g_input           = vidioc_g_input,
    .vidioc_s_input           = vidioc_s_input,
    .vidioc_queryctrl         = vidioc_queryctrl,
    .vidioc_g_ctrl            = vidioc_g_ctrl,
    .vidioc_s_ctrl            = vidioc_s_ctrl,
    .vidioc_streamon          = vidioc_streamon,
    .vidioc_streamoff         = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
    .vidiocgmbuf              = vidiocgmbuf,
#endif
};
#endif

static struct video_device coach_template = {
    .name	= "coach",
    .fops       = &coach_fops,
    .minor	= -1,
    .release	= video_device_release,

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
    .ioctl_ops 	= &coach_ioctl_ops,
#else
    .vidioc_querycap          = vidioc_querycap,
    .vidioc_enum_fmt_cap      = vidioc_enum_fmt_vid_cap,
    .vidioc_g_fmt_cap         = vidioc_g_fmt_vid_cap,
    .vidioc_try_fmt_cap       = vidioc_try_fmt_vid_cap,
    .vidioc_s_fmt_cap 	      = vidioc_s_fmt_vid_cap,
    .vidioc_reqbufs           = vidioc_reqbufs,
    .vidioc_querybuf          = vidioc_querybuf,
    .vidioc_qbuf              = vidioc_qbuf,
    .vidioc_dqbuf             = vidioc_dqbuf,
    .vidioc_s_std             = vidioc_s_std,
    .vidioc_enum_input        = vidioc_enum_input,
    .vidioc_g_input           = vidioc_g_input,
    .vidioc_s_input           = vidioc_s_input,
    .vidioc_queryctrl         = vidioc_queryctrl,
    .vidioc_g_ctrl            = vidioc_g_ctrl,
    .vidioc_s_ctrl            = vidioc_s_ctrl,
    .vidioc_streamon          = vidioc_streamon,
    .vidioc_streamoff         = vidioc_streamoff,
#ifdef CONFIG_VIDEO_V4L1_COMPAT
    .vidiocgmbuf              = vidiocgmbuf,
#endif
#endif
};

/* -----------------------------------------------------------------
	Initialization and module stuff
   ------------------------------------------------------------------*/
static int coach_board_init(struct coach_dev *cam)
{
    struct coach_pipeinfo *pipe = cam->pipe;
    unsigned long i;
    uint16_t mode[2];

    DBG("board init: %p\n", cam);
    memset(pipe, 0, sizeof(*pipe));
    pipe->cam = cam;
    pipe->transfer_size = BUFFER_SIZE;

    pipe->transfer_buffer = kzalloc(pipe->transfer_size, GFP_KERNEL);
    if (pipe->transfer_buffer == NULL) {
        DBG("out of memory!\n");
        return -ENOMEM;
    }

    cam->b_acquire = 0;
    cam->frame_count = 0;

    /*** start create system buffers ***/
    for (i = 0; i < FRAMES; i++) {
        /* always allocate maximum size for system buffers */
        cam->buffer.frame[i].lpvbits = vmalloc(MAX_FRAME_SIZE);

        DBG("valloc %p, idx %lu, pdata %p\n",
                &cam->buffer.frame[i], i,
                cam->buffer.frame[i].lpvbits);
        if (cam->buffer.frame[i].lpvbits == NULL) {
            printk(KERN_INFO KBUILD_MODNAME ": out of memory. "
                    "Using less frames\n");
            break;
        }
    }

    if (i == 0) {
        printk(KERN_INFO KBUILD_MODNAME ": out of memory. Aborting\n");
        kfree(cam->pipe->transfer_buffer);
        cam->pipe->transfer_buffer = NULL;
        return -ENOMEM;
    } else
        cam->buffer.dwFrames = i;

    /* make sure internal states are set */
    for (i = 0; i < FRAMES; i++) {
        cam->buffer.frame[i].ulState = ZR364XX_READ_IDLE;
        cam->buffer.frame[i].cur_size = 0;
    }

    cam->cur_frame = 0;
    cam->last_frame = -1;
    /*** end create system buffers ***/

    /* start read pipe */
    coach_set_param(cam->udev, PRMID_REQ_STREAM, 0);
    coach_set_param(cam->udev, PRMID_STREAM_WIDTH, 320);
    coach_set_param(cam->udev, PRMID_STREAM_HEIGHT, 240);
    coach_set_param(cam->udev, PRMID_STREAM_RATE, 30);
    coach_set_param(cam->udev, PRMID_STREAM_CR, 16);

    coach_get_param(cam->udev, PRMID_HCE_MODE, mode);
    dprintk(cam, 0, "board initialized. HCE MODE %d\n", *mode);
//    printk(KERN_DEBUG "cam->vfd: %u\n",cam->vfd);
//    printk(KERN_DEBUG "cam->udev: %u\n",cam->udev);
//    printk(KERN_DEBUG "cam->interface: %u\n",cam->interface);
//    printk(KERN_DEBUG "cam->pipe: %u\n",cam->pipe);

    return 0;
}

static int coach_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
    struct coach_dev *cam = NULL;
    struct usb_device *udev = interface_to_usbdev(intf);
    struct usb_host_interface *iface_desc;
    struct usb_endpoint_descriptor *endpoint;
    int err, i;

    DBG("probing...\n");

    dev_info(&intf->dev, COACH_MODULE_NAME " plugged\n");
    dev_info(&intf->dev, "model %04x:%04x detected\n",
            le16_to_cpu(udev->descriptor.idVendor),
            le16_to_cpu(udev->descriptor.idProduct));

    cam = kzalloc(sizeof(struct coach_dev), GFP_KERNEL);
    if (cam == NULL) {
        dev_err(&udev->dev, "cam: out of memory !\n");
        return -ENOMEM;
    }

    cam->vfd = video_device_alloc();
    if (cam->vfd == NULL) {
        dev_err(&udev->dev, "cam->vfd: out of memory !\n");
        kfree(cam);
        cam = NULL;
        return -ENOMEM;
    }
    memcpy(cam->vfd, &coach_template, sizeof(coach_template));
    video_set_drvdata(cam->vfd, cam);
    //if (debug)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
        cam->vfd->dev_debug = V4L2_DEV_DEBUG_IOCTL | V4L2_DEV_DEBUG_IOCTL_ARG;
#else
        cam->vfd->debug = V4L2_DEBUG_IOCTL | V4L2_DEBUG_IOCTL_ARG;
#endif

    cam->udev = udev;
    DBG("dev: %p, udev %p interface %p\n", cam, cam->udev, intf);

    cam->users = 0;
    cam->removed = 0;
    cam->nb = 0;

    /* initialize locks */
    mutex_init(&cam->mutex);
    mutex_init(&cam->open_lock);

    // set up the endpoint information
    iface_desc = intf->cur_altsetting;
    DBG("num endpoints %d\n", iface_desc->desc.bNumEndpoints);
    for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
        endpoint = &iface_desc->endpoint[i].desc;
        if (!cam->read_endpoint && usb_endpoint_is_bulk_in(endpoint)) {
            // we found the bulk in endpoint 
            cam->read_endpoint = endpoint->bEndpointAddress;
        }
    }

    if (!cam->read_endpoint) {
        dev_err(&intf->dev, "Could not find bulk-in endpoint\n");
        return -ENOMEM;
    }

    /* v4l init video dma queues */
    INIT_LIST_HEAD(&cam->vidq.active);

    cam->vidq.cam = cam;
    err = video_register_device(cam->vfd, VFL_TYPE_GRABBER, -1);
    if (err) {
        dev_err(&udev->dev, "video_register_device failed\n");
        video_device_release(cam->vfd);
        kfree(cam);
        cam = NULL;
        return err;
    }

    usb_set_intfdata(intf, cam);

    // start pipeline.
    err = coach_board_init(cam);
//    printk(KERN_DEBUG "cam->vfd: %u\n",cam->vfd);
//    printk(KERN_DEBUG "cam->udev: %u\n",cam->udev);
//    printk(KERN_DEBUG "cam->interface: %u\n",cam->interface);
//    printk(KERN_DEBUG "cam->pipe: %u\n",cam->pipe);
    if (err) {
        dprintk(cam, 0, "Error!\n");
        spin_lock_init(&cam->slock);
        return err;
    }
    spin_lock_init(&cam->slock);
    dprintk(cam, 0, "spin success\n");
    dprintk(cam, 0, "cam pointer : %u\n",cam);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 33)
    dev_info(&udev->dev, COACH_MODULE_NAME " controlling device %s\n", video_device_node_name(cam->vfd));
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 31)
    dev_info(&udev->dev, COACH_MODULE_NAME " controlling device %d\n", cam->vfd->num);
#else
    dev_info(&udev->dev, COACH_MODULE_NAME " controlling device\n");
#endif
    dprintk(cam, 0, "No error occured\n");
    return 0;
}

static void coach_destroy(struct coach_dev *dev)
{
    int i;

    if (!dev) {
        printk(KERN_ERR KBUILD_MODNAME ", %s: no device\n", __func__);
        return;
    }

    mutex_lock(&dev->open_lock);

    if (dev->b_acquire)
        zr364xx_stop_acquire(dev);

    if(dev->removed == 0)
        zr364xx_stop_readpipe(dev);

    if (dev->vfd)
        video_unregister_device(dev->vfd);
    dev->vfd = NULL;

    /* release sys buffers */
    for (i = 0; i < FRAMES; i++) {
        if (dev->buffer.frame[i].lpvbits) {
            DBG("vfree %p\n", dev->buffer.frame[i].lpvbits);
            vfree(dev->buffer.frame[i].lpvbits);
        }
        dev->buffer.frame[i].lpvbits = NULL;
    }

    mutex_unlock(&dev->open_lock);
    kfree(dev);
}

static void coach_disconnect(struct usb_interface *intf)
{
    struct coach_dev *dev = usb_get_intfdata(intf);
    usb_set_intfdata(intf, NULL);
    dev_info(&intf->dev, COACH_MODULE_NAME " unplugged\n");
    if(dev->users == 0)
	coach_destroy(dev);
    else
	dev->removed = 1;
}

static struct usb_driver coach_driver = {
    .name = "coach",
    .probe = coach_probe,
    .disconnect = coach_disconnect,
    .id_table = device_table
};

static int __init coach_init(void)
{
    int retval = 0;
    retval = usb_register(&coach_driver);
    if (retval)
        printk(KERN_ERR KBUILD_MODNAME ": usb_register failed!\n");
    else
        printk(KERN_INFO KBUILD_MODNAME ": " COACH_MODULE_NAME "\n");

    return retval;
}

static void __exit coach_exit(void)
{
    printk(KERN_INFO KBUILD_MODNAME ": " COACH_MODULE_NAME " module unloaded\n");
    usb_deregister(&coach_driver);
}

module_init(coach_init);
module_exit(coach_exit);
