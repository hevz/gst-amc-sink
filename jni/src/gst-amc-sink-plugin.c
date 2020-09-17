/*
 ============================================================================
 Name        : gst-amc-sink-plugin.c
 Author      : Heiher <r@hev.cc>
 Version     : 0.0.1
 Copyright   : Copyright (C) 2014 everyone.
 Description : 
 ============================================================================
 */

#include "gst-amc-sink-plugin.h"
#include "gst-amc.h"

#define PACKAGE "gstamcsink"
#define VERSION "0.0.1"

static gboolean plugin_init (GstPlugin *);

GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,  /* major */
    GST_VERSION_MINOR,  /* minor */
    amcsink,            /* short unique name */
    "Android Media codec video sink",   /* info */
    plugin_init,    /* GstPlugin::plugin_init */
    VERSION,        /* version */
    "LGPL",          /* license */
    PACKAGE, /* package-name, usually the file archive name */
    "https://gstreamer.freedesktop.org" /* origin */
    )

static gboolean
plugin_init (GstPlugin *plugin)
{
    if (!gst_amc_init ())
      return FALSE;

    if (!gst_element_register (plugin, "amcvideosink",
                    GST_RANK_NONE, GST_TYPE_AMC_VIDEO_SINK))
      return FALSE;

    return TRUE;
}

/* vim:set tabstop=4 softtabstop=4 shiftwidth=4 expandtab: */

