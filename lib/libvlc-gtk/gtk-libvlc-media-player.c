/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * freetuxtv
 * Copyright (C) Eric Beuque 2010 <eric.beuque@gmail.com>
	 * 
 * freetuxtv is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
	 * 
 * freetuxtv is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "gtk-libvlc-media-player.h"
#include "gtk-libvlc-private.h"

#include <gdk/gdkx.h>

typedef struct _GtkLibvlcMediaPlayerPrivate GtkLibvlcMediaPlayerPrivate;
struct _GtkLibvlcMediaPlayerPrivate
{
	gboolean initialized;

	GtkTreePath *current_media;
	gchar **current_options;

	gboolean is_media_parsed;
	gboolean play_next_at_end;
	gboolean loop;

#ifndef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_media_player_t *libvlc_mediaplayer;
#endif // LIBVLC_DEPRECATED_PLAYLIST
};

#define GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GTK_TYPE_LIBVLC_MEDIA_PLAYER, GtkLibvlcMediaPlayerPrivate))

G_DEFINE_TYPE (GtkLibvlcMediaPlayer, gtk_libvlc_media_player, GTK_TYPE_WIDGET);

#ifndef LIBVLC_DEPRECATED_PLAYLIST
static void 
on_vlc_event(const libvlc_event_t *event, void *user_data);
#endif // LIBVLC_DEPRECATED_PLAYLIST

static gboolean
raise_error(GtkLibvlcMediaPlayer *self, GError** error, gpointer user_data)
{
	gboolean bRes = FALSE;
	const gchar *szErrMsg;

#ifdef LIBVLC_OLD_VLCEXCEPTION
	g_return_if_fail(user_data != NULL);
	libvlc_exception_t *ex = (libvlc_exception_t*)user_data;
	if (libvlc_exception_raised (ex)){
		bRes = TRUE;

		szErrMsg = libvlc_exception_get_message(ex);
		if(error != NULL){
			*error = g_error_new (GTK_LIBVLC_ERROR,
			                      GTK_LIBVLC_ERROR_LIBVLC,
			                      "%s", szErrMsg);
		}
	}
#else
	szErrMsg = libvlc_errmsg();
	if(szErrMsg){
		bRes = TRUE;
		if(error != NULL){
			*error = g_error_new (GTK_LIBVLC_ERROR,
			                      GTK_LIBVLC_ERROR_LIBVLC,
			                      "%s", szErrMsg);
		}
	}
#endif // LIBVLC_OLD_VLCEXCEPTION

	return bRes;
}

static void
gtk_libvlc_media_player_init (GtkLibvlcMediaPlayer *object)
{
	object->libvlc_instance = NULL;

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(object);

#ifndef LIBVLC_DEPRECATED_PLAYLIST
	priv->libvlc_mediaplayer = NULL;
	priv->is_media_parsed = FALSE;
#endif // LIBVLC_DEPRECATED_PLAYLIST

	object->media_list = NULL;

	priv->initialized = FALSE;
	priv->play_next_at_end = TRUE;
	priv->loop = FALSE;

	priv->current_options = NULL;
}

static void
gtk_libvlc_media_player_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(widget));
	g_return_if_fail(requisition != NULL);

	requisition->width = 240;
	requisition->height = 160;
}

static void
gtk_libvlc_media_player_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(widget));
	g_return_if_fail(allocation != NULL);

	widget->allocation = *allocation;

	if (GTK_WIDGET_REALIZED(widget)) {
		gdk_window_move_resize(widget->window,
		                       allocation->x, allocation->y,
		                       allocation->width, allocation->height);
	}
}

static void
gtk_libvlc_media_player_realize(GtkWidget *widget)
{
	GtkLibvlcMediaPlayer *libvlcmp = GTK_LIBVLC_MEDIA_PLAYER (widget);
	GdkWindowAttr attributes;
	guint attributes_mask;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(widget));

	if (GTK_WIDGET_NO_WINDOW (widget)){
		GTK_WIDGET_CLASS (gtk_libvlc_media_player_parent_class)->realize (widget);
	}else{
		GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

		attributes.window_type = GDK_WINDOW_CHILD;
		attributes.x = widget->allocation.x;
		attributes.y = widget->allocation.y;
		attributes.width = widget->allocation.width;
		attributes.height = widget->allocation.height;		
		attributes.wclass = GDK_INPUT_OUTPUT;
		attributes.visual = gtk_widget_get_visual (widget);
		attributes.colormap = gtk_widget_get_colormap (widget);		
		attributes.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK;

		attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;

		widget->window = gdk_window_new(gtk_widget_get_parent_window (widget),
		                                & attributes, attributes_mask);

		gdk_window_set_user_data(widget->window, libvlcmp);

		widget->style = gtk_style_attach(widget->style, widget->window);
		gtk_style_set_background(widget->style, widget->window, GTK_STATE_ACTIVE);
	}
}

static void
gtk_libvlc_media_player_finalize (GObject *object)
{
	GtkLibvlcMediaPlayer *self;
	GError *error = NULL;

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	g_return_if_fail(object != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(object));

	self = GTK_LIBVLC_MEDIA_PLAYER(object);

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	priv->play_next_at_end = FALSE;

	// Release the media player
#ifndef LIBVLC_DEPRECATED_PLAYLIST
	if(priv->libvlc_mediaplayer != NULL){
		// Detach events on the media player
		libvlc_event_manager_t *em;
#ifdef LIBVLC_OLD_VLCEXCEPTION
		em = libvlc_media_player_event_manager (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);

		libvlc_event_detach (em, libvlc_MediaPlayerNothingSpecial, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_detach (em, libvlc_MediaPlayerOpening, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_detach (em, libvlc_MediaPlayerBuffering, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_detach (em, libvlc_MediaPlayerPlaying, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_detach (em, libvlc_MediaPlayerPaused, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_detach (em, libvlc_MediaPlayerStopped, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_detach (em, libvlc_MediaPlayerEndReached, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_detach (em, libvlc_MediaPlayerEncounteredError, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);

		// Stop the current media
		libvlc_media_player_stop (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
#else
		em = libvlc_media_player_event_manager (priv->libvlc_mediaplayer);
		raise_error(self, &error, NULL);

		libvlc_event_detach (em, libvlc_MediaPlayerNothingSpecial, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_detach (em, libvlc_MediaPlayerOpening, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_detach (em, libvlc_MediaPlayerBuffering, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_detach (em, libvlc_MediaPlayerPlaying, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_detach (em, libvlc_MediaPlayerPaused, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_detach (em, libvlc_MediaPlayerStopped, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_detach (em, libvlc_MediaPlayerEndReached, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_detach (em, libvlc_MediaPlayerEncounteredError, on_vlc_event, self);
		raise_error(self, &error, NULL);

		// Stop the current media
		libvlc_media_player_stop (priv->libvlc_mediaplayer);
		raise_error(self, &error, NULL);
#endif // LIBVLC_OLD_VLCEXCEPTION

		libvlc_media_player_release (priv->libvlc_mediaplayer);
		priv->libvlc_mediaplayer = NULL;
	}
#endif // LIBVLC_DEPRECATED_PLAYLIST

	if(priv->current_options != NULL){
		g_strfreev (priv->current_options);
		priv->current_options = NULL;
	}

	// Free the media list
	gtk_libvlc_media_player_clear_media_list (self);

	G_OBJECT_CLASS (gtk_libvlc_media_player_parent_class)->finalize (object);

	if(error != NULL){
		g_error_free (error);
		error = NULL;
	}
}

static void
gtk_libvlc_media_player_class_init (GtkLibvlcMediaPlayerClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass* parent_class = GTK_WIDGET_CLASS (klass);

	g_type_class_add_private (klass, sizeof (GtkLibvlcMediaPlayerPrivate));

	object_class->finalize = gtk_libvlc_media_player_finalize;

	parent_class->realize = gtk_libvlc_media_player_realize;
	parent_class->size_request = gtk_libvlc_media_player_size_request;
	parent_class->size_allocate = gtk_libvlc_media_player_size_allocate;
}

static void
gtk_libvlc_media_player_initialize(GtkLibvlcMediaPlayer *self)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	g_return_if_fail(self->libvlc_instance != NULL);
	
	GError *error = NULL;

	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, &error);
	g_return_if_fail(libvlc_instance != NULL);	


	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	if(priv->initialized == FALSE){

#ifdef LIBVLC_DEPRECATED_PLAYLIST
		XID xid = gdk_x11_drawable_get_xid(GTK_WIDGET(self)->window);
		libvlc_video_set_parent(libvlc_instance, xid, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
#else
		XID xid = gdk_x11_drawable_get_xid(GTK_WIDGET(self)->window);

#ifdef LIBVLC_OLD_VLCEXCEPTION
		priv->libvlc_mediaplayer = libvlc_media_player_new (libvlc_instance, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);

#ifdef LIBVLC_OLD_SET_DRAWABLE
		libvlc_media_player_set_drawable (priv->libvlc_mediaplayer, xid,
		                                  &_vlcexcep);
#else
		libvlc_media_player_set_xwindow (priv->libvlc_mediaplayer, xid,
		                                 &_vlcexcep);
#endif // LIBVLC_OLD_SET_DRAWABLE
		raise_error(self, &error, &_vlcexcep);

		// Attach events on the media player
		libvlc_event_manager_t *em;
		em = libvlc_media_player_event_manager (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerNothingSpecial, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerOpening, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerBuffering, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerPlaying, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerPaused, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerStopped, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerEndReached, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		libvlc_event_attach (em, libvlc_MediaPlayerEncounteredError, on_vlc_event, self, &_vlcexcep);
		raise_error(self, &error, &_vlcexcep);
		//libvlc_event_attach (em, libvlc_MediaPlayerPositionChanged, on_vlc_event, self, &_vlcexcep);
		//raise_error(self, error, &_vlcexcep);

#else
		priv->libvlc_mediaplayer = libvlc_media_player_new (libvlc_instance);
		raise_error(self, &error, NULL);

		libvlc_media_player_set_xwindow (priv->libvlc_mediaplayer, xid);
		raise_error(self, &error, NULL);

		// Attach events on the media player
		libvlc_event_manager_t *em;
		em = libvlc_media_player_event_manager (priv->libvlc_mediaplayer);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerNothingSpecial, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerOpening, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerBuffering, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerPlaying, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerPaused, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerStopped, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerEndReached, on_vlc_event, self);
		raise_error(self, &error, NULL);
		libvlc_event_attach (em, libvlc_MediaPlayerEncounteredError, on_vlc_event, self);
		raise_error(self, &error, NULL);
		//libvlc_event_attach (em, libvlc_MediaPlayerPositionChanged, on_vlc_event, self, &_vlcexcep);
		//raise_error(self, error, &_vlcexcep);

#endif // LIBVLC_OLD_VLCEXCEPTION

#endif // LIBVLC_DEPRECATED_PLAYLIST

		priv->initialized = TRUE;	
	}

	if(error != NULL){
		g_error_free (error);
		error = NULL;
	}
}

static void
gtk_libvlc_media_player_play_media(GtkLibvlcMediaPlayer *self, GtkLibvlcMedia *media, gchar **options, GError** error)
{
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);

	GtkLibvlcMediaPlayerPrivate *priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif

	int nb_options;
	int nb_m_options = 0; // Number of media options
	int nb_mp_options = 0; // Number of media player options
	gchar** list_options = NULL;
	int i;

	gtk_libvlc_media_player_stop (self, error);

	// g_print("\n\nPlaying : %s\n\n", media->mrl);
	// Delete options attached to the media player
	if(priv->current_options != options){
		if(priv->current_options != NULL){
			g_strfreev (priv->current_options);
			priv->current_options = NULL;
		}
		if(options == NULL){
			priv->current_options = NULL;
		}else{
			priv->current_options = g_strdupv (options);
		}
	}

	// Merge options in only one tab
	gchar** media_options;
	media_options = (gchar**)gtk_libvlc_media_get_options(media);
	if(media_options != NULL){
		nb_m_options += g_strv_length(media_options);
	}
	if(priv->current_options != NULL){
		nb_mp_options += g_strv_length(priv->current_options);
	}
	nb_options = nb_m_options + nb_mp_options;
	if(nb_options > 0){
		list_options = (gchar**)g_malloc((nb_options+1) * sizeof(gchar*));
		list_options[nb_options] = NULL;
		int step = 0;
		// We put first the media option
		for(i=0; i<nb_m_options; i++){
			list_options[step+i] = g_strdup(media_options[i]);
		}
		step += nb_m_options;
		// After we put the media player option
		for(i=0; i<nb_mp_options; i++){
			list_options[step+i] = g_strdup(priv->current_options[i]);
		}
	}


	// Play the media
#ifdef LIBVLC_DEPRECATED_PLAYLIST
	if(libvlc_playlist_items_count (libvlc_instance, &_vlcexcep) > 0){
		libvlc_playlist_delete_item(libvlc_instance, 0, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
		libvlc_playlist_clear(libvlc_instance, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
	if(list_options != NULL){

		//int i;
		//for(i=0; i<nb_options; i++){
		//	 g_print("option[%d] = %s\n", i, list_options[i]);
		//}

		libvlc_playlist_add_extended(libvlc_instance, media->mrl, NULL,
		                             nb_options, (const char**)list_options, &_vlcexcep);
	}else{
		libvlc_playlist_add (libvlc_instance, media->mrl, NULL, &_vlcexcep);		
	}
	raise_error(self, error, &_vlcexcep);

	if (libvlc_playlist_items_count (libvlc_instance, &_vlcexcep) > 0){;
		libvlc_playlist_play (libvlc_instance, -1, 0, 
		                      NULL, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_t *m;
	m = libvlc_media_new (libvlc_instance, media->mrl, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);

	if(list_options != NULL){
		int i=0;
		for(i=0; i<nb_options; i++){
			libvlc_media_add_option(m, g_strdup(list_options[i]), &_vlcexcep);
			raise_error(self, error, &_vlcexcep);
			//g_print("option_copy[%d] = %s\n", i, list_options[i]);
		}
	}	

	libvlc_media_player_set_media (priv->libvlc_mediaplayer, m, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_media_release (m);

	// Event on the media
	libvlc_event_manager_t *em;
	m = libvlc_media_player_get_media(priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	em = libvlc_media_event_manager (m, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_event_attach (em, libvlc_MediaSubItemAdded, on_vlc_event, self, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);

	libvlc_media_player_play (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else 
	libvlc_media_t *m;
	m = libvlc_media_new_path (libvlc_instance, media->mrl);
	raise_error(self, error, NULL);

	if(list_options != NULL){
		int i=0;
		for(i=0; i<nb_options; i++){
			libvlc_media_add_option(m, g_strdup(list_options[i]));
			raise_error(self, error, NULL);
			//g_print("option_copy[%d] = %s\n", i, list_options[i]);
		}
	}	

	libvlc_media_player_set_media (priv->libvlc_mediaplayer, m);
	raise_error(self, error, NULL);
	libvlc_media_release (m);

	// Event on the media
	libvlc_event_manager_t *em;
	m = libvlc_media_player_get_media(priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
	em = libvlc_media_event_manager (m);
	raise_error(self, error, NULL);
	libvlc_event_attach (em, libvlc_MediaSubItemAdded, on_vlc_event, self);
	raise_error(self, error, NULL);

	libvlc_media_player_play (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
#endif

#endif

	// Free the options tab
	if(list_options){
		g_strfreev(list_options);
		list_options=NULL;
	}
}

static void
gtk_libvlc_media_player_set_current_path(GtkLibvlcMediaPlayer *self, GtkTreePath *path)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	if(priv->current_media != path){
		if(priv->current_media != NULL){
			gtk_tree_path_free(priv->current_media);	
		}
		if(path != NULL){
			priv->current_media = gtk_tree_path_copy(path);
		}else{
			priv->current_media = NULL;
		}
	}
}

#ifndef LIBVLC_DEPRECATED_PLAYLIST

static gboolean
idle_play_next_function(gpointer ptrdata){
	GtkLibvlcMediaPlayer *self;
	g_return_if_fail(ptrdata != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(ptrdata));

	self = GTK_LIBVLC_MEDIA_PLAYER(ptrdata);

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	GError* error = NULL;

	gtk_libvlc_media_player_play_next (self, priv->current_options, &error);

	if(error != NULL){
		g_error_free (error);
		error = NULL;
	}

	return FALSE;
}

static void 
on_vlc_event(const libvlc_event_t *event, void *user_data)
{
	//g_print("event %s\n", libvlc_event_type_name (event->type));

	GtkLibvlcMediaPlayer *self;
	g_return_if_fail(user_data != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(user_data));

	self = GTK_LIBVLC_MEDIA_PLAYER(user_data);

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	if(event->type == libvlc_MediaSubItemAdded){		

		libvlc_media_t *new_m;
		new_m = event->u.media_subitem_added.new_child;

		if(new_m != NULL){
			
#ifdef LIBVLC_OLD_VLCEXCEPTION
			gchar* mrl = libvlc_media_get_mrl(new_m,
			                                  &_vlcexcep);
#else
			gchar* mrl = libvlc_media_get_mrl(new_m);
#endif // LIBVLC_OLD_VLCEXCEPTION
			
			GtkLibvlcMedia *new_media;
			new_media = gtk_libvlc_media_new(mrl);

			GtkTreeIter iter1;
			GtkTreeIter iter2;

			gtk_tree_model_get_iter (GTK_TREE_MODEL(self->media_list), &iter1, priv->current_media);

			if(priv->is_media_parsed == FALSE){
				gtk_tree_store_append (self->media_list, &iter2, &iter1);
				gtk_tree_store_set (GTK_TREE_STORE(self->media_list), &iter2,
				                    GTK_LIBVLC_MODEL_MEDIA_COLUMN, new_media, -1);
			}
		}		
	}	

	if(event->type == libvlc_MediaPlayerEndReached){
		if(priv->play_next_at_end == TRUE){
			g_idle_add (idle_play_next_function, (gpointer)self);
		}
	}
}

#endif // LIBVLC_DEPRECATED_PLAYLIST

GtkWidget *
gtk_libvlc_media_player_new (GtkLibvlcInstance* libvlc_instance, GError** error)
{
	g_return_if_fail(libvlc_instance != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_INSTANCE(libvlc_instance));

	GtkLibvlcMediaPlayer *self = NULL;
	self = gtk_type_new (GTK_TYPE_LIBVLC_MEDIA_PLAYER);

	self->libvlc_instance = libvlc_instance;
	g_object_ref(G_OBJECT(self->libvlc_instance));

	self->media_list = gtk_tree_store_new(GTK_LIBVLC_MODEL_NB_COLUMN, GTK_TYPE_LIBVLC_MEDIA);

	return GTK_WIDGET(self);
}

void
gtk_libvlc_media_player_add_media (GtkLibvlcMediaPlayer *self, GtkLibvlcMedia *media)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	// Add the media in the media list
	GtkTreeIter iter;
	gtk_tree_store_append (self->media_list, &iter, NULL);
	gtk_tree_store_set (GTK_TREE_STORE(self->media_list), &iter,
	                    GTK_LIBVLC_MODEL_MEDIA_COLUMN, media, -1);
}

GtkLibvlcMedia*
gtk_libvlc_media_player_get_current_media (GtkLibvlcMediaPlayer *self)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	return gtk_libvlc_media_player_get_media_from_path(self, priv->current_media);
}

GtkLibvlcMedia*
gtk_libvlc_media_player_get_media_from_path (GtkLibvlcMediaPlayer *self, GtkTreePath *path)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	g_return_if_fail(path != NULL);

	// Get the media from the path
	GtkLibvlcMedia *media;
	GtkTreeIter iter;
	if(gtk_tree_model_get_iter (GTK_TREE_MODEL(self->media_list), &iter, path)){
		gtk_tree_model_get (GTK_TREE_MODEL(self->media_list),
		                    &iter, 0, &media, -1);
		return media;
	}

	return NULL;
}

void
gtk_libvlc_media_player_clear_media_list(GtkLibvlcMediaPlayer *self)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	gtk_tree_store_clear (self->media_list);

	gtk_libvlc_media_player_set_current_path(self, NULL);
}

void
gtk_libvlc_media_player_play (GtkLibvlcMediaPlayer *self, gchar **options, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	g_return_if_fail(self->libvlc_instance != NULL);

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

	// Play the current media or the first if no current media
	GtkTreePath *path = priv->current_media;
	if(path == NULL){
		path = gtk_tree_path_new_first();
		gtk_libvlc_media_player_set_current_path(self, path);
		gtk_tree_path_free(path);
	}
	gtk_libvlc_media_player_play_media_at_path(self, priv->current_media, options, error);
}

void
gtk_libvlc_media_player_play_media_at_path (GtkLibvlcMediaPlayer *self, GtkTreePath *path, gchar **options, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	g_return_if_fail(self->libvlc_instance != NULL);
	g_return_if_fail(path != NULL);

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

	gtk_libvlc_media_player_set_current_path(self, path);

	GtkLibvlcMedia *media;
	media = gtk_libvlc_media_player_get_media_from_path (self, priv->current_media);

	GtkTreeIter iter;
	g_return_if_fail(gtk_tree_model_get_iter (GTK_TREE_MODEL(self->media_list), &iter, priv->current_media) == TRUE);
	if(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(self->media_list), &iter)){
		priv->is_media_parsed = TRUE;
	}else{
		priv->is_media_parsed = FALSE;
	}

	// Play the media
	gtk_libvlc_media_player_play_media (self, media, options, error);
}

void
gtk_libvlc_media_player_play_next (GtkLibvlcMediaPlayer *self, gchar **options, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	g_return_if_fail(self->libvlc_instance != NULL);

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

	GtkTreePath* path = NULL;
	if(priv->current_media != NULL){

		GtkTreeIter iter1;
		GtkTreeIter iter2;
		g_return_if_fail(gtk_tree_model_get_iter (GTK_TREE_MODEL(self->media_list), &iter1, priv->current_media) == TRUE);
		if(gtk_tree_model_iter_has_child(GTK_TREE_MODEL(self->media_list), &iter1)){
			gtk_tree_model_iter_children(GTK_TREE_MODEL(self->media_list), &iter2, &iter1);
			path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->media_list), &iter2);
		}else{
			if(gtk_tree_model_iter_next (GTK_TREE_MODEL(self->media_list), &iter1)){
				path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->media_list), &iter1);
			}else{
				gtk_tree_model_get_iter (GTK_TREE_MODEL(self->media_list), &iter1, priv->current_media);
				if(gtk_tree_model_iter_parent (GTK_TREE_MODEL(self->media_list), &iter2, &iter1)){
					if(gtk_tree_model_iter_next (GTK_TREE_MODEL(self->media_list), &iter2)){
						path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->media_list), &iter2);
					}else{
						if(priv->loop){
							gtk_tree_model_get_iter_first(GTK_TREE_MODEL(self->media_list), &iter1);
							path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->media_list), &iter1);
						}
					}
				}else{
					if(priv->loop){
						gtk_tree_model_get_iter_first(GTK_TREE_MODEL(self->media_list), &iter1);
						path = gtk_tree_model_get_path(GTK_TREE_MODEL(self->media_list), &iter1);
					}
				}
			}
		}

		// Play the next media
		if(path != NULL){
			gtk_libvlc_media_player_play_media_at_path(self, path, options, error);
		}
	}else{
		// Play the first media
		if(priv->loop){
			gtk_libvlc_media_player_play(self, options, error);
		}
	}	
}

void
gtk_libvlc_media_player_pause (GtkLibvlcMediaPlayer *self, GError **error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);
	
	libvlc_playlist_pause (libvlc_instance, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else
#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_player_pause (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else
	libvlc_media_player_pause (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
#endif // LIBVLC_OLD_VLCEXCEPTION
#endif // LIBVLC_DEPRECATED_PLAYLIST

}

gboolean
gtk_libvlc_media_player_can_pause (GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

	gboolean ret = FALSE;

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	ret = TRUE;
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	ret = libvlc_media_player_can_pause (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else
	ret = libvlc_media_player_can_pause (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);

#endif // LIBVLC_OLD_VLCEXCEPTION	

#endif // LIBVLC_DEPRECATED_PLAYLIST

	return ret;

}

void
gtk_libvlc_media_player_stop (GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

	// Stop the current playing
#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);

	if (libvlc_playlist_isplaying (libvlc_instance, &_vlcexcep)) {
		raise_error(self, error, &_vlcexcep);
		libvlc_playlist_stop (libvlc_instance, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
#else
	if(priv->libvlc_mediaplayer != NULL){
#ifdef LIBVLC_OLD_VLCEXCEPTION
		libvlc_media_player_stop (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
#else
		libvlc_media_player_stop (priv->libvlc_mediaplayer);
		raise_error(self, error, NULL);
#endif // LIBVLC_OLD_VLCEXCEPTION
	}
#endif // LIBVLC_DEPRECATED_PLAYLIST
}

void
gtk_libvlc_media_player_set_volume (GtkLibvlcMediaPlayer *self, gdouble volume, GError** error)
{

	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	g_return_if_fail(volume >= 0 && volume <= LIBVLC_MAX_VOLUME_POWER);

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);

	libvlc_audio_set_volume (libvlc_instance, 
	                         (gint)(volume*100), &_vlcexcep);    
	raise_error(self, error, &_vlcexcep);
#else
	libvlc_audio_set_volume (priv->libvlc_mediaplayer, 
	                         (gint)(volume*100));    
	raise_error(self, error, NULL);

#endif // LIBVLC_OLD_VLCEXCEPTION


}

gdouble
gtk_libvlc_media_player_get_volume (GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);

	gint volume;
	volume = libvlc_audio_get_volume (libvlc_instance, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else
	gint volume;
	volume = libvlc_audio_get_volume (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
#endif // LIBVLC_OLD_VLCEXCEPTION

	return (gdouble)volume/100;
}

void
gtk_libvlc_media_player_set_fullscreen (GtkLibvlcMediaPlayer *self, gboolean fullscreen, GError **error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_input_t *input_t;
	if (libvlc_playlist_isplaying (libvlc_instance, &_vlcexcep)) {
		input_t = libvlc_playlist_get_input(libvlc_instance,
		                                    &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
		libvlc_set_fullscreen(input_t, fullscreen, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
		libvlc_input_free(input_t);
	}
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	if(priv->libvlc_mediaplayer != NULL){
		libvlc_set_fullscreen (priv->libvlc_mediaplayer, fullscreen, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
#else
	if(priv->libvlc_mediaplayer != NULL){
		libvlc_set_fullscreen (priv->libvlc_mediaplayer, fullscreen);
		raise_error(self, error, NULL);
	}

#endif // LIBVLC_OLD_VLCEXCEPTION

#endif // LIBVLC_DEPRECATED_PLAYLIST
}

gboolean
gtk_libvlc_media_player_is_playing (GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	int res;

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);

	res = libvlc_playlist_isplaying(libvlc_instance, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	return (res == 1);
#else
#ifdef LIBVLC_DONT_HAVE_ISPLAYING
	return (gtk_libvlc_media_player_get_state(self)==GTK_LIBVLC_STATE_PLAYING);
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	res = libvlc_media_player_is_playing (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	return (res == 1);
#else
	res = libvlc_media_player_is_playing (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
	return (res == 1);
#endif

#endif // LIBVLC_DONT_HAVE_ISPLAYING

#endif // LIBVLC_DEPRECATED_PLAYLIST
}

GtkLibvlcState
gtk_libvlc_media_player_get_state (GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	// Create the media player if not initialized
	gtk_libvlc_media_player_initialize (self);
	g_return_if_fail(priv->initialized == TRUE);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	GtkLibvlcState gtkstate = GTK_LIBVLC_STATE_NOTHING_SPECIAL;

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = (libvlc_instance_t *)
		gtk_libvlc_instance_get_libvlc_instance(self->libvlc_instance, error);
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_input_t *input_t;
	if(libvlc_instance != NULL){
		if (libvlc_playlist_isplaying (libvlc_instance, &_vlcexcep)) {
			input_t = libvlc_playlist_get_input(libvlc_instance,
			                                    &_vlcexcep);
			raise_error(self, error, &_vlcexcep);
			int state = libvlc_input_get_state(input_t, &_vlcexcep);
			raise_error(self, error, &_vlcexcep);
			switch(state){
				case 0 :
					gtkstate = GTK_LIBVLC_STATE_NOTHING_SPECIAL;
					break;
				case 1 :
					gtkstate = GTK_LIBVLC_STATE_OPENING;
					break;
				case 2 :
					gtkstate = GTK_LIBVLC_STATE_BUFFERING;	
					break;
				case 3 :
					gtkstate = GTK_LIBVLC_STATE_PLAYING;
					break;
				case 4 :
					gtkstate = GTK_LIBVLC_STATE_PAUSED;
					break;
				case 5 :
					gtkstate = GTK_LIBVLC_STATE_STOPPED;
					break;
				case 6 :
					gtkstate = GTK_LIBVLC_STATE_ENDED;
					break;
				case 7 :
					gtkstate = GTK_LIBVLC_STATE_ERROR;
					break;
			}
			libvlc_input_free(input_t);
		}
	}
#else
	g_return_if_fail(priv->libvlc_mediaplayer != NULL);

	libvlc_state_t state;

#ifdef LIBVLC_OLD_VLCEXCEPTION
	state = libvlc_media_player_get_state (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else
	state = libvlc_media_player_get_state (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
#endif // LIBVLC_OLD_VLCEXCEPTION	

	switch(state){
		case libvlc_NothingSpecial :
			gtkstate = GTK_LIBVLC_STATE_NOTHING_SPECIAL;
			break;
		case libvlc_Opening :
			gtkstate = GTK_LIBVLC_STATE_OPENING;
			break;
		case libvlc_Buffering :
			gtkstate = GTK_LIBVLC_STATE_BUFFERING;	
			break;
		case libvlc_Playing :
			gtkstate = GTK_LIBVLC_STATE_PLAYING;
			break;
		case libvlc_Paused :
			gtkstate = GTK_LIBVLC_STATE_PAUSED;
			break;
		case libvlc_Stopped :
			gtkstate = GTK_LIBVLC_STATE_STOPPED;
			break;
		case libvlc_Ended :
			gtkstate = GTK_LIBVLC_STATE_ENDED;
			break;
		case libvlc_Error:
			gtkstate = GTK_LIBVLC_STATE_ERROR;
			break;
	}
#endif // LIBVLC_DEPRECATED_PLAYLIST
	return gtkstate;

}

const gchar*
gtk_libvlc_media_player_state_tostring (GtkLibvlcState state)
{
	switch(state){
		case GTK_LIBVLC_STATE_NOTHING_SPECIAL :
			return "GTK_LIBVLC_STATE_NOTHING_SPECIAL";
		case GTK_LIBVLC_STATE_OPENING :
			return "GTK_LIBVLC_STATE_OPENING";
		case GTK_LIBVLC_STATE_BUFFERING :
			return "GTK_LIBVLC_STATE_BUFFERING";
		case GTK_LIBVLC_STATE_PLAYING :
			return "GTK_LIBVLC_STATE_PLAYING";
		case GTK_LIBVLC_STATE_PAUSED :
			return "GTK_LIBVLC_STATE_PAUSED";
		case GTK_LIBVLC_STATE_STOPPED :
			return "GTK_LIBVLC_STATE_STOPPED";
		case GTK_LIBVLC_STATE_ENDED :
			return "GTK_LIBVLC_STATE_ENDED";
		case GTK_LIBVLC_STATE_ERROR:
			return "GTK_LIBVLC_STATE_ERROR";
	}
}

glong
gtk_libvlc_media_player_get_length(GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	glong length = 0;

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = self->libvlc_instance->libvlc_instance;
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_input_t *input_t;
	input_t = libvlc_playlist_get_input(libvlc_instance,
	                                    &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	length = libvlc_input_get_length(input_t, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_input_free(input_t);
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	if(media != NULL){
		length = libvlc_media_player_get_length (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
#else
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
	if(media != NULL){
		length = libvlc_media_player_get_length (priv->libvlc_mediaplayer);
		raise_error(self, error, NULL);
	}
#endif // LIBVLC_OLD_VLCEXCEPTION

#endif // LIBVLC_DEPRECATED_PLAYLIST
	return length;
}

glong
gtk_libvlc_media_player_get_time(GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	
	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	glong time = 0;

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = self->libvlc_instance->libvlc_instance;
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_input_t *input_t;
	input_t = libvlc_playlist_get_input(libvlc_instance,
	                                    &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	time = libvlc_input_get_time(input_t, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_input_free(input_t);
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	if(media != NULL){
		time = libvlc_media_player_get_time (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
#else
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
	if(media != NULL){
		time = libvlc_media_player_get_time (priv->libvlc_mediaplayer);
		raise_error(self, error, NULL);
	}
#endif // LIBVLC_OLD_VLCEXCEPTION	

#endif // LIBVLC_DEPRECATED_PLAYLIST
	return time;
}

void
gtk_libvlc_media_player_set_time(GtkLibvlcMediaPlayer *self, glong time, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	
	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION	

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = self->libvlc_instance->libvlc_instance;
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_input_t *input_t;
	input_t = libvlc_playlist_get_input(libvlc_instance,
	                                    &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_input_set_time(input_t, time, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_input_free(input_t);
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_player_set_time (priv->libvlc_mediaplayer, time, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else
	libvlc_media_player_set_time (priv->libvlc_mediaplayer, time);
	raise_error(self, error, NULL);
#endif // LIBVLC_OLD_VLCEXCEPTION

#endif // LIBVLC_DEPRECATED_PLAYLIST
}

gfloat
gtk_libvlc_media_player_get_position(GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	
	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	gfloat pos = 0.0;

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = self->libvlc_instance->libvlc_instance;
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_input_t *input_t;
	input_t = libvlc_playlist_get_input(libvlc_instance,
	                                    &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	pos = libvlc_input_get_position(input_t, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_input_free(input_t);
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	if(media != NULL){
		pos = libvlc_media_player_get_position (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
#else
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
	if(media != NULL){
		pos = libvlc_media_player_get_position (priv->libvlc_mediaplayer);
		raise_error(self, error, NULL);
	}
#endif // LIBVLC_OLD_VLCEXCEPTION	

#endif // LIBVLC_DEPRECATED_PLAYLIST
	return pos;

}

void
gtk_libvlc_media_player_set_position(GtkLibvlcMediaPlayer *self, gfloat position, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	
	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	g_return_if_fail(position>=0.0 && position<=1.0);	

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	libvlc_instance_t *libvlc_instance;
	libvlc_instance = self->libvlc_instance->libvlc_instance;
	g_return_if_fail(libvlc_instance != NULL);

	libvlc_input_t *input_t;
	input_t = libvlc_playlist_get_input(libvlc_instance,
	                                    &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_input_set_position(input_t, position, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	libvlc_input_free(input_t);

#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_player_set_position (priv->libvlc_mediaplayer, position, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
#else
	libvlc_media_player_set_position (priv->libvlc_mediaplayer, position);
	raise_error(self, error, NULL);
#endif // LIBVLC_OLD_VLCEXCEPTION

#endif // LIBVLC_DEPRECATED_PLAYLIST
}

gboolean
gtk_libvlc_media_player_is_seekable (GtkLibvlcMediaPlayer *self, GError** error)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_exception_t _vlcexcep;
	libvlc_exception_init (&_vlcexcep);
#endif // LIBVLC_OLD_VLCEXCEPTION

	gboolean ret = FALSE;

#ifdef LIBVLC_DEPRECATED_PLAYLIST
	ret = TRUE;
#else

#ifdef LIBVLC_OLD_VLCEXCEPTION
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer, &_vlcexcep);
	raise_error(self, error, &_vlcexcep);
	if(media != NULL){
		ret = libvlc_media_player_is_seekable (priv->libvlc_mediaplayer, &_vlcexcep);
		raise_error(self, error, &_vlcexcep);
	}
#else
	libvlc_media_t* media;
	media = libvlc_media_player_get_media (priv->libvlc_mediaplayer);
	raise_error(self, error, NULL);
	if(media != NULL){
		ret = libvlc_media_player_is_seekable (priv->libvlc_mediaplayer);
		raise_error(self, error, NULL);
	}
#endif // LIBVLC_OLD_VLCEXCEPTION

#endif // LIBVLC_DEPRECATED_PLAYLIST
	return ret;
}

void
gtk_libvlc_media_player_set_play_next_at_end (GtkLibvlcMediaPlayer *self, gboolean b)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	priv->play_next_at_end = b;	
}

void
gtk_libvlc_media_player_set_loop (GtkLibvlcMediaPlayer *self, gboolean b)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));

	GtkLibvlcMediaPlayerPrivate* priv;
	priv = GTK_LIBVLC_MEDIA_PLAYER_PRIVATE(self);

	priv->loop = b;	
}

GtkLibvlcInstance*
gtk_libvlc_media_player_get_instance (GtkLibvlcMediaPlayer *self)
{
	g_return_if_fail(self != NULL);
	g_return_if_fail(GTK_IS_LIBVLC_MEDIA_PLAYER(self));
	return self->libvlc_instance;
}