#include "cpu.h"
#include "mode.h"

static void cpugraph_construct( XfcePanelPlugin *plugin )
{
	CPUGraph *base;

	xfce_textdomain( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR, "UTF-8" );

	base = CreateControl( plugin );

	ReadSettings( plugin, base );

	g_signal_connect( plugin, "free-data", G_CALLBACK( Kill ), base );

	g_signal_connect( plugin, "save", G_CALLBACK( WriteSettings ), base );

	xfce_panel_plugin_menu_show_configure( plugin );

	g_signal_connect( plugin, "configure-plugin", G_CALLBACK( CreateOptions ), base );

	g_signal_connect( plugin, "size-changed", G_CALLBACK( SetSize ), base );

	g_signal_connect( plugin, "orientation-changed", G_CALLBACK( SetOrientation ), base );
}

/* register the plugin */
XFCE_PANEL_PLUGIN_REGISTER_EXTERNAL( cpugraph_construct );

void Kill( XfcePanelPlugin * plugin, CPUGraph * base )
{
	gint i;
	cpuData_free();
	base->m_CpuData = NULL;

	for(i=0; i<base->nrCores-1; i++)
		gtk_widget_destroy(base->m_pBar[i]);
	g_free( base->m_pBar );

	gtk_widget_destroy(base->m_Box);

	if( base->m_TimeoutID )
		g_source_remove( base->m_TimeoutID );

	g_free( base->m_History );

	g_object_unref( base->m_Tooltip );

	g_free( base->m_AssociateCommand );
	g_free( base );
}


CPUGraph * CreateControl( XfcePanelPlugin * plugin )
{
	gint i;
	GtkWidget *frame, *ebox;
	GtkOrientation orientation;
	GtkProgressBarOrientation barOrientation;
	CPUGraph *base = g_new0( CPUGraph, 1 );

	base->plugin = plugin;

	ebox = gtk_event_box_new();
	gtk_widget_show( ebox );
	gtk_container_add( GTK_CONTAINER( plugin ), ebox );

	orientation = xfce_panel_plugin_get_orientation(plugin);
	if(orientation == GTK_ORIENTATION_HORIZONTAL)
		barOrientation = GTK_PROGRESS_BOTTOM_TO_TOP;
	else
		barOrientation = GTK_PROGRESS_LEFT_TO_RIGHT;

	base->m_Box = xfce_hvbox_new(orientation, FALSE, 0);
	gtk_widget_show(base->m_Box);
	gtk_container_add(GTK_CONTAINER(ebox), base->m_Box);

	gtk_container_set_border_width(GTK_CONTAINER(frame), BORDER / 2);


	/* Multicore stuff */
	if((base->nrCores = cpuData_init() - 1) < 0)
		fprintf(stderr,"Cannot init cpu data !\n");

	base->m_pBar = (GtkWidget **) g_malloc( sizeof( GtkWidget * ) * base->nrCores );

	for(i=0; i<base->nrCores; i++) {
		base->m_pBar[i] = GTK_WIDGET(gtk_progress_bar_new());
		gtk_progress_bar_set_orientation(
				GTK_PROGRESS_BAR(base->m_pBar[i]),
				barOrientation);

		gtk_box_pack_start(
				GTK_BOX(base->m_Box),
				base->m_pBar[i],
				FALSE,
				FALSE,
				0);

		gtk_widget_show(base->m_pBar[i]);
	}

	base->m_FrameWidget = frame = gtk_frame_new( NULL );
	gtk_frame_set_shadow_type( GTK_FRAME( frame ), GTK_SHADOW_IN );

	gtk_box_pack_start( GTK_BOX(base->m_Box), frame, TRUE, TRUE, 2);

	gtk_widget_show( frame );

	xfce_panel_plugin_add_action_widget( plugin, ebox );
	g_signal_connect( ebox, "button-press-event", G_CALLBACK( LaunchCommand ), base );

	base->m_DrawArea = gtk_drawing_area_new();
	gtk_widget_set_app_paintable( base->m_DrawArea, TRUE );
	gtk_container_add( GTK_CONTAINER( frame ), GTK_WIDGET( base->m_DrawArea ) );
	gtk_widget_show( base->m_DrawArea );


	xfce_panel_plugin_add_action_widget( plugin, base->m_DrawArea );

	base->m_Tooltip = gtk_tooltips_new();
	g_object_ref( base->m_Tooltip );
	gtk_object_sink( GTK_OBJECT( base->m_Tooltip ) );

	g_signal_connect_after( base->m_DrawArea, "expose-event", G_CALLBACK( DrawAreaExposeEvent ), base );

	return base;
}

void SetOrientation( XfcePanelPlugin * plugin, GtkOrientation orientation, CPUGraph *base )
{
	GtkProgressBarOrientation barOrientation;
	gpointer p_pBar[base->nrCores];
	gpointer p_FrameWidget;
	gint i; 
	/* <-- Multicore stuff */

	orientation = xfce_panel_plugin_get_orientation( plugin );
	if( orientation == GTK_ORIENTATION_HORIZONTAL )
		barOrientation = GTK_PROGRESS_BOTTOM_TO_TOP;
	else
		barOrientation = GTK_PROGRESS_LEFT_TO_RIGHT;

	/* Unpack progress bars */
	for( i=0; i<base->nrCores; i++ )
	{
		/* reference progress bars to keep them alive */
		p_pBar[i] = g_object_ref( base->m_pBar[i] );
		gtk_container_remove( GTK_CONTAINER( base->m_Box ), GTK_WIDGET( base->m_pBar[i] ) );
	}
	p_FrameWidget = g_object_ref( base->m_FrameWidget );
	gtk_container_remove( GTK_CONTAINER( base->m_Box ), GTK_WIDGET( base->m_FrameWidget ) );


	xfce_hvbox_set_orientation( XFCE_HVBOX( base->m_Box ), orientation );

	/* Pack progress bars again into hvbox */
	for( i=0; i<base->nrCores; i++ )
	{
		gtk_progress_bar_set_orientation( GTK_PROGRESS_BAR( base->m_pBar[i] ), barOrientation );	
		gtk_box_pack_start( GTK_BOX( base->m_Box ), base->m_pBar[i], FALSE, FALSE, 1 );
		/* We dont need anymore this reference */
		g_object_unref( p_pBar[i] );
	}
	gtk_box_pack_start( GTK_BOX( base->m_Box ), base->m_FrameWidget, TRUE, TRUE, 2 );
	g_object_unref( p_FrameWidget );
	UserSetSize( base );
	gtk_widget_queue_draw( base->m_DrawArea );
}

void UpdateTooltip( CPUGraph * base )
{
	char tooltip[32];
	int pos = snprintf( tooltip, 32, "Usage: %d%%", base->m_CpuData[0].load*100/CPU_SCALE );
	if( base->m_CpuData[0].scalCurFreq )
		snprintf( tooltip+pos, 32-pos, " (%d MHz)", base->m_CpuData[0].scalCurFreq/1000 );
	gtk_tooltips_set_tip( GTK_TOOLTIPS( base->m_Tooltip ), base->m_FrameWidget->parent, tooltip, NULL );
}

gboolean SetSize( XfcePanelPlugin *plugin, int size, CPUGraph *base )
{
	gint i;
	gtk_container_set_border_width( GTK_CONTAINER( base->m_FrameWidget ), size > 26 ? 2 : 0 );

	if( xfce_panel_plugin_get_orientation( plugin ) == GTK_ORIENTATION_HORIZONTAL )
	{
		gtk_widget_set_size_request( GTK_WIDGET( plugin ), base->m_Width, size );
		for( i=0; i<base->nrCores; i++ )
			gtk_widget_set_size_request( GTK_WIDGET(base->m_pBar[i]), BORDER, size );
	}
	else
	{
		gtk_widget_set_size_request( GTK_WIDGET( plugin ), size, base->m_Width );
		for( i=0; i<base->nrCores; i++ )
			gtk_widget_set_size_request( GTK_WIDGET( base->m_pBar[i] ), size, BORDER );
	}

	return TRUE;
}

void UserSetSize( CPUGraph * base )
{
	SetSize( base->plugin, xfce_panel_plugin_get_size( base->plugin ), base );
}

gboolean UpdateCPU( CPUGraph * base )
{
	gint i;
	base->m_CpuData = cpuData_read();
	for( i=0; i<base->nrCores; i++ )
	{
		gtk_progress_bar_set_fraction( GTK_PROGRESS_BAR(base->m_pBar[i]), (gdouble)base->m_CpuData[i+1].load / CPU_SCALE );
	}

	if( base->m_TimeScale )
	{
		int i = base->m_Values - 1;
		int j = i + base->m_Values;
		while( i > 0 )
		{
			int a, b;

			a = base->m_History[i], b = base->m_History[i-1];
			if( a < b ) a++;
			int factor = (i*2);
			base->m_History[i--] = (a * (factor-1) + b) / factor;

			a = base->m_History[j], b = base->m_History[j-1];
			if( a < b ) a++;
			base->m_History[j--] = (a * (factor-1) + b) / factor;
		}
	} else {
		memmove( base->m_History + 1 , base->m_History , (base->m_Values * 2 - 1) * sizeof( int ) );
	}
	base->m_History[0] = (long)base->m_CpuData[0].load;
	base->m_History[base->m_Values] = base->m_CpuData[0].scalCurFreq;

	/* Tooltip */
	UpdateTooltip( base );

	/* Draw the graph. */
	gtk_widget_queue_draw( base->m_DrawArea );
	return TRUE;
}

void DrawGraph( CPUGraph * base )
{
	GtkWidget *da = base->m_DrawArea;
	int w, h;

	w = da->allocation.width;
	h = da->allocation.height;

	/* Dynamically allocated everytime just in case depth changes */
	GdkGC *fg1 = gdk_gc_new( da->window );
	GdkGC *fg2 = gdk_gc_new( da->window );
	GdkGC *bg = gdk_gc_new( da->window );
	gdk_gc_set_rgb_fg_color( bg, &base->m_BackGround );

	gdk_draw_rectangle( da->window, bg, TRUE, 0, 0, w, h );

	if( base->m_Mode == 0 )
	{
		drawGraphModeNormal( base, fg1, da, w, h );
	}
	else if( base->m_Mode == 1 )
	{
		drawGraphModeLED( base, fg1, fg2, da, w, h );
	}
	else if( base->m_Mode == 2 )
	{
		drawGraphModeNoHistory( base, fg1, fg2, da, w, h );
	}

	g_object_unref( fg2 );
	g_object_unref( fg1 );
	g_object_unref( bg );
}

void DrawAreaExposeEvent( GtkWidget * da, GdkEventExpose * event, gpointer data )
{
	CPUGraph *base = (CPUGraph *) data;

	DrawGraph( base );
}

gboolean LaunchCommand( GtkWidget*w,GdkEventButton *event, CPUGraph *base )
{
	if( event->button == 1 )
	{
		GString *cmd;
		if( strlen(base->m_AssociateCommand) == 0 )
		{
			return;
		}
		cmd = g_string_new( base->m_AssociateCommand );
		xfce_exec( cmd->str, FALSE, FALSE, NULL );
		g_string_free( cmd, TRUE );
	}
	return FALSE;
}
