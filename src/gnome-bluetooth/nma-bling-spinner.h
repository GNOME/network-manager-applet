/* @Copyright (C) 2007 John Stowers, Neil Jagdish Patel.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 */


#ifndef _NMA_BLING_SPINNER_H_
#define _NMA_BLING_SPINNER_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define NMA_TYPE_BLING_SPINNER           (nma_bling_spinner_get_type ())
#define NMA_BLING_SPINNER(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), NMA_TYPE_BLING_SPINNER, NmaBlingSpinner))
#define NMA_BLING_SPINNER_CLASS(obj)     (G_TYPE_CHECK_CLASS_CAST ((obj), NMA_BLING_SPINNER,  NmaBlingSpinnerClass))
#define NMA_IS_BLING_SPINNER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), NMA_TYPE_BLING_SPINNER))
#define NMA_IS_BLING_SPINNER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE ((obj), NMA_TYPE_BLING_SPINNER))
#define NMA_BLING_SPINNER_GET_CLASS      (G_TYPE_INSTANCE_GET_CLASS ((obj), NMA_TYPE_BLING_SPINNER, NmaBlingSpinnerClass))

typedef struct _NmaBlingSpinner      NmaBlingSpinner;
typedef struct _NmaBlingSpinnerClass NmaBlingSpinnerClass;
typedef struct _NmaBlingSpinnerPrivate  NmaBlingSpinnerPrivate;

struct _NmaBlingSpinner
{
	GtkDrawingArea parent;
};

struct _NmaBlingSpinnerClass
{
	GtkDrawingAreaClass parent_class;
	NmaBlingSpinnerPrivate *priv;
};

GType nma_bling_spinner_get_type (void);

GtkWidget * nma_bling_spinner_new (void);

void nma_bling_spinner_start (NmaBlingSpinner *spinner);
void nma_bling_spinner_stop  (NmaBlingSpinner *spinner);

G_END_DECLS

#endif
