/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8-*- */

#ifndef GTK_LIBVLC_MEDIA_PLAYER_H
#define GTK_LIBVLC_MEDIA_PLAYER_H

#include <gtk/gtk.h>

#include "gtk-libvlc-include.h"
#include "gtk-libvlc-instance.h"
#include "gtk-libvlc-media.h"

G_BEGIN_DECLS

#define GTK_TYPE_LIBVLC_MEDIA_PLAYER            (gtk_libvlc_media_player_get_type ())
#define GTK_LIBVLC_MEDIA_PLAYER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_LIBVLC_MEDIA_PLAYER, GtkLibVLCMediaPlayer))
#define GTK_LIBVLC_MEDIA_PLAYER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_LIBVLC_MEDIA_PLAYER, GtkLibVLCMediaPlayerClass))
#define GTK_IS_LIBVLC_MEDIA_PLAYER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_LIBVLC_MEDIA_PLAYER))
#define GTK_IS_LIBVLC_MEDIA_PLAYER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_LIBVLC_MEDIA_PLAYER))
#define GTK_LIBVLC_MEDIA_PLAYER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_LIBVLC_MEDIA_PLAYER, GtkLibVLCMediaPlayerClass))

typedef struct _GtkLibVLCMediaPlayer GtkLibVLCMediaPlayer;
typedef struct _GtkLibVLCMediaPlayerClass GtkLibVLCMediaPlayerClass;

struct _GtkLibVLCMediaPlayer
{
	GtkWidget parent;

	GtkLibVLCInstance* libvlc_instance;
	
#if LIBVLC_VERSION_MAJOR == 0 && LIBVLC_VERSION_MINOR == 8
#else
	libvlc_media_player_t *libvlc_mediaplayer;
#endif
	
	GtkTreeStore *media_list;
};

struct _GtkLibVLCMediaPlayerClass
{
	GtkWidgetClass parent_class;
};

#define GTK_LIBVLC_STATE_TYPE (gtk_libvlc_state_type_get_type())

typedef enum {
	GTK_LIBVLC_STATE_NOTHING_SPECIAL,
	GTK_LIBVLC_STATE_OPENING,
	GTK_LIBVLC_STATE_BUFFERING,
	GTK_LIBVLC_STATE_PLAYING,
	GTK_LIBVLC_STATE_PAUSED,
	GTK_LIBVLC_STATE_STOPPED,
	GTK_LIBVLC_STATE_ENDED,
	GTK_LIBVLC_STATE_ERROR
} GtkLibVLCState;

enum {
	GTK_LIBVLC_MODEL_MEDIA_COLUMN,
	GTK_LIBVLC_MODEL_NB_COLUMN
};

GType
gtk_libvlc_media_player_get_type (void);

GtkWidget *
gtk_libvlc_media_player_new (GtkLibVLCInstance* libvlc_instance);

void
gtk_libvlc_media_player_add_media (GtkLibVLCMediaPlayer *self, GtkLibVLCMedia *media);

void
gtk_libvlc_media_player_clear_media_list(GtkLibVLCMediaPlayer *self);

void
gtk_libvlc_media_player_play (GtkLibVLCMediaPlayer *self);

void
gtk_libvlc_media_player_stop (GtkLibVLCMediaPlayer *self);

void
gtk_libvlc_media_player_set_volume (GtkLibVLCMediaPlayer *self, gdouble volume);

gdouble
gtk_libvlc_media_player_get_volume (GtkLibVLCMediaPlayer *self);

void
gtk_libvlc_media_player_set_fullscreen (GtkLibVLCMediaPlayer *self, gboolean fullscreen);

void
gtk_libvlc_media_player_record_current (GtkLibVLCMediaPlayer *self, gchar* output_filename);

gboolean
gtk_libvlc_media_player_is_recording (GtkLibVLCMediaPlayer *self);

GtkLibVLCState
gtk_libvlc_media_player_get_state (GtkLibVLCMediaPlayer *self);

const gchar*
gtk_libvlc_media_player_state_tostring (GtkLibVLCState state);

GtkLibVLCInstance*
gtk_libvlc_media_player_get_instance (GtkLibVLCMediaPlayer *self);

G_END_DECLS

#endif /* GTK_LIBVLC_MEDIA_PLAYER_H */