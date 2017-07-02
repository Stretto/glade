/*
 * glade-intro.c
 *
 * Copyright (C) 2017 Juan Pablo Ugarte
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public 
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Authors:
 *   Juan Pablo Ugarte <juanpablougarte@gmail.com>
 */

#include "glade-intro.h"

typedef struct
{
  GtkWidget   *widget;
  const gchar *name;
  const gchar *text;
  gint         delay;
} ScriptNode;

typedef struct
{
  GtkWidget  *toplevel;

  GList      *script;      /* List of (ScriptNode *) */
  GHashTable *widgets;     /* Table with all named widget in toplevel */

  GtkPopover *popover;     /* Popover to show the script text */
  GtkLabel   *label;       /* Label to display script text */

  guint       timeout_id;  /* Timeout id for running the script */
  GList      *current;     /* Current script node */
} GladeIntroPrivate;

struct _GladeIntro
{
  GObject parent_instance;
};

enum
{
  PROP_0,
  PROP_TOPLEVEL,

  N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

G_DEFINE_TYPE_WITH_PRIVATE (GladeIntro, glade_intro, G_TYPE_OBJECT);

#define GET_PRIVATE(d) ((GladeIntroPrivate *) glade_intro_get_instance_private((GladeIntro*)d))

static void
widget_add_style_class (GtkWidget *widget, const gchar *klass)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  gtk_style_context_add_class (context, klass);
}

static void
widget_remove_style_class (GtkWidget *widget, const gchar *klass)
{
  GtkStyleContext *context = gtk_widget_get_style_context (widget);
  gtk_style_context_remove_class (context, klass);
}

static void
glade_intro_init (GladeIntro *intro)
{
  GladeIntroPrivate *priv = GET_PRIVATE (intro);
  GtkWidget *box, *image;

  priv->popover = GTK_POPOVER (g_object_ref_sink (gtk_popover_new (NULL)));
  priv->label = GTK_LABEL (gtk_label_new (""));
  gtk_label_set_line_wrap (priv->label, TRUE);
  gtk_label_set_max_width_chars (priv->label, 28);
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  image = gtk_image_new_from_icon_name ("dialog-information-symbolic", GTK_ICON_SIZE_DIALOG);

  gtk_box_pack_start (GTK_BOX (box), image, FALSE, FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (priv->label), FALSE, FALSE, 0);
  gtk_container_add (GTK_CONTAINER (priv->popover), box);

  widget_add_style_class (GTK_WIDGET (priv->popover), "glade-intro");
  gtk_widget_show_all (box);
}

static void
glade_intro_finalize (GObject *object)
{
  GladeIntroPrivate *priv = GET_PRIVATE (object);

  if (priv->timeout_id)
    {
      g_source_remove (priv->timeout_id);
      priv->timeout_id = 0;
    }

  gtk_popover_set_relative_to (priv->popover, NULL);
  g_clear_object (&priv->popover);
  priv->label = NULL;

  G_OBJECT_CLASS (glade_intro_parent_class)->finalize (object);
}

static void
glade_intro_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  g_return_if_fail (GLADE_IS_INTRO (object));

  switch (prop_id)
    {
      case PROP_TOPLEVEL:
        glade_intro_set_toplevel (GLADE_INTRO (object), g_value_get_object (value));
      break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
glade_intro_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GladeIntroPrivate *priv;

  g_return_if_fail (GLADE_IS_INTRO (object));
  priv = GET_PRIVATE (object);

  switch (prop_id)
    {
      case PROP_TOPLEVEL:
        g_value_set_object (value, priv->toplevel);
      break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
glade_intro_class_init (GladeIntroClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = glade_intro_finalize;
  object_class->set_property = glade_intro_set_property;
  object_class->get_property = glade_intro_get_property;

  /* Properties */
  properties[PROP_TOPLEVEL] =
    g_param_spec_object ("toplevel", "Toplevel",
                         "The main toplevel from where to get the widgets",
                         GTK_TYPE_WINDOW,
                         G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, N_PROPERTIES, properties);
}

/* Public API */

GladeIntro *
glade_intro_new (GtkWindow *toplevel)
{
  return (GladeIntro*) g_object_new (GLADE_TYPE_INTRO, "toplevel", toplevel, NULL);
}

static void
get_toplevel_widgets (GtkWidget *widget, gpointer data)
{
  const gchar *name;

  if ((name = gtk_widget_get_name (widget)) && 
      g_strcmp0 (name, G_OBJECT_TYPE_NAME (widget)))
    g_hash_table_insert (GET_PRIVATE (data)->widgets, (gpointer)name, widget);

  if (GTK_IS_CONTAINER (widget))
    gtk_container_forall (GTK_CONTAINER (widget), get_toplevel_widgets, data);
}

void
glade_intro_set_toplevel (GladeIntro *intro, GtkWindow *toplevel)
{
  GladeIntroPrivate *priv;

  g_return_if_fail (GLADE_IS_INTRO (intro));
  priv = GET_PRIVATE (intro);

  g_clear_object (&priv->toplevel);
  g_clear_pointer (&priv->widgets, g_hash_table_unref);

  if (toplevel)
    {
      priv->toplevel = g_object_ref (toplevel);
      priv->widgets = g_hash_table_new (g_str_hash, g_str_equal);
      gtk_container_forall (GTK_CONTAINER (toplevel), get_toplevel_widgets, intro);
    }
}

void
glade_intro_script_add (GladeIntro  *intro,
                        const gchar *name,
                        const gchar *text,
                        gint         delay)
{
  GladeIntroPrivate *priv;
  ScriptNode *node;

  g_return_if_fail (GLADE_IS_INTRO (intro));
  priv = GET_PRIVATE (intro);

  node = g_new0 (ScriptNode, 1);
  node->name  = name;
  node->text  = text;
  node->delay = delay;

  priv->script = g_list_append (priv->script, node);
}

static gboolean script_play (gpointer data);

static gboolean
script_transition (gpointer data)
{
  GladeIntroPrivate *priv = GET_PRIVATE (data);
  ScriptNode *node;

  if (priv->current && (node = priv->current->data))
    widget_remove_style_class (node->widget, "glade-intro-highlight");

  priv->timeout_id = 0;
  gtk_popover_popdown (priv->popover);
  priv->timeout_id = g_timeout_add (250, script_play, data);

  return G_SOURCE_REMOVE;
}

static gboolean
script_play (gpointer data)
{
  GladeIntroPrivate *priv = GET_PRIVATE (data);
  ScriptNode *node;

  priv->timeout_id = 0;

  if (priv->script == NULL)
    return G_SOURCE_REMOVE;

  /* Set current node to prenset */
  priv->current = (priv->current) ? g_list_next (priv->current) : priv->script;

  if (!priv->current ||
      !(node = priv->current->data) || 
      (!node->widget && !(node->widget = g_hash_table_lookup (priv->widgets, node->name))))
    return G_SOURCE_REMOVE;

  widget_add_style_class (node->widget, "glade-intro-highlight");
  gtk_label_set_text (priv->label, node->text);
  gtk_popover_set_relative_to (priv->popover, node->widget);
  gtk_popover_popup (priv->popover);

  priv->timeout_id = g_timeout_add_seconds (node->delay, script_transition, data);
  
  return G_SOURCE_REMOVE;
}

void
glade_intro_play (GladeIntro *intro)
{
  g_return_if_fail (GLADE_IS_INTRO (intro));

  script_play (intro);
}

void
glade_intro_pause (GladeIntro *intro)
{
  GladeIntroPrivate *priv;

  g_return_if_fail (GLADE_IS_INTRO (intro));
  priv = GET_PRIVATE (intro);

  if (priv->timeout_id)
    g_source_remove (priv->timeout_id);

  priv->timeout_id = 0;
  gtk_popover_popdown (priv->popover);
}

void
glade_intro_stop (GladeIntro *intro)
{
  GladeIntroPrivate *priv;

  g_return_if_fail (GLADE_IS_INTRO (intro));
  priv = GET_PRIVATE (intro);
  
  glade_intro_pause (intro);
  priv->current = NULL;
}
