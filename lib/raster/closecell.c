/*!
 * \file raster/closecell.c
 * 
 * \brief Raster Library - Close raster file
 *
 * (C) 1999-2009 by the GRASS Development Team
 *
 * This program is free software under the GNU General Public
 * License (>=v2). Read the file COPYING that comes with GRASS
 * for details.
 *
 * \author USACERL and many others
 */

#ifdef __MINGW32__
#  include <windows.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

#include <grass/gis.h>
#include <grass/raster.h>
#include <grass/glocale.h>

#include "R.h"

#define FORMAT_FILE "f_format"
#define QUANT_FILE  "f_quant"
#define NULL_FILE   "null"

static int close_old(int);
static int close_new(int, int);

static int write_fp_format(int fd);

/*!
 * \brief Close a raster map
 *
 * The raster map opened on file descriptor <i>fd</i> is
 * closed. Memory allocated for raster processing is freed. If open
 * for writing, skeletal support files for the new raster map are
 * created as well.
 *
 * <b>Note:</b> If a module wants to explicitly write support files
 * (e.g., a specific color table) for a raster map it creates, it must
 * do so after the raster map is closed. Otherwise the close will
 * overwrite the support files. See \ref
 * Raster_Map_Layer_Support_Routines for routines which write raster
 * support files.
 *
 * If the map is a new floating point, move the <tt>.tmp</tt> file
 * into the <tt>fcell</tt> element, create an empty file in the
 * <tt>cell</tt> directory; write the floating-point range file; write
 * a default quantization file quantization file is set here to round
 * fp numbers (this is a default for now). create an empty category
 * file, with max cat = max value (for backwards compatibility). Move
 * the <tt>.tmp</tt> NULL-value bitmap file to the <tt>cell_misc</tt>
 * directory.
 *
 * \param fd file descriptor
 *
 * \return -1 on error
 * \return 1 on success
 */
int Rast_close_cell(int fd)
{
    struct fileinfo *fcb = &R__.fileinfo[fd];

    if (fd < 0 || fd >= R__.fileinfo_count || fcb->open_mode <= 0)
	return -1;
    if (fcb->open_mode == OPEN_OLD)
	return close_old(fd);

    return close_new(fd, 1);
}

/*!
 * \brief Unopen a raster map
 *
 * The raster map opened on file descriptor <i>fd</i> is
 * closed. Memory allocated for raster processing is freed. If open
 * for writing, the raster map is not created and the temporary file
 * created when the raster map was opened is removed (see \ref
 * Creating_and_Opening_New_Raster_Files). This routine is useful when
 * errors are detected and it is desired to not create the new raster
 * map. While it is true that the raster map will not be created if
 * the module exits without closing the file, the temporary file will
 * not be removed at module exit. GRASS database management will
 * eventually remove the temporary file, but the file can be quite
 * large and will take up disk space until GRASS does remove it. Use
 * this routine as a courtesy to the user.
 *
 * \param fd file descriptor
 *
 * \return -1 on error
 * \return 1 on success
 */
int Rast_unopen_cell(int fd)
{
    struct fileinfo *fcb = &R__.fileinfo[fd];

    if (fd < 0 || fd >= R__.fileinfo_count || fcb->open_mode <= 0)
	return -1;
    if (fcb->open_mode == OPEN_OLD)
	return close_old(fd);
    else
	return close_new(fd, 0);
}

static int close_old(int fd)
{
    struct fileinfo *fcb = &R__.fileinfo[fd];
    int i;

    /* if R__.auto_mask was only allocated for reading map rows to create
       non-existant null rows, and not for actuall mask, free R__.mask_row 
       if(R__.auto_mask <=0)
       G_free (R__.mask_buf);
       This is obsolete since now the mask_bus is always allocated
     */

    if (fcb->gdal)
	Rast_close_gdal_link(fcb->gdal);

    for (i = 0; i < NULL_ROWS_INMEM; i++)
	G_free(fcb->NULL_ROWS[i]);

    if (fcb->cellhd.compressed)
	G_free(fcb->row_ptr);
    G_free(fcb->col_map);
    G_free(fcb->mapset);
    G_free(fcb->data);
    G_free(fcb->name);
    if (fcb->reclass_flag)
	Rast_free_reclass(&fcb->reclass);
    fcb->open_mode = -1;

    if (fcb->map_type != CELL_TYPE) {
	Rast_quant_free(&fcb->quant);
	xdr_destroy(&fcb->xdrstream);
    }
    close(fd);

    return 1;
}

static void write_support_files(int fd)
{
    struct fileinfo *fcb = &R__.fileinfo[fd];
    struct Categories cats;
    struct History hist;
    CELL cell_min, cell_max;
    char path[GPATH_MAX];

    /* remove color table */
    Rast_remove_colors(fcb->name, "");

    /* create a history file */
    Rast_short_history(fcb->name, "raster", &hist);
    Rast_write_history(fcb->name, &hist);

    /* write the range */
    if (fcb->map_type == CELL_TYPE) {
	Rast_write_range(fcb->name, &fcb->range);
	Rast__remove_fp_range(fcb->name);
    }
    /*NOTE: int range for floating point maps is not written out */
    else {			/* if(fcb->map_type != CELL_TYPE) */

	Rast_write_fp_range(fcb->name, &fcb->fp_range);
	Rast_construct_default_range(&fcb->range);
	/* this range will be used to add default rule to quant structure */
    }

    if (fcb->map_type != CELL_TYPE)
	fcb->cellhd.format = -1;
    else			/* CELL map */
	fcb->cellhd.format = fcb->nbytes - 1;

    /* write header file */
    Rast_put_cellhd(fcb->name, &fcb->cellhd);

    /* if map is floating point write the quant rules, otherwise remove f_quant */
    if (fcb->map_type != CELL_TYPE) {
	/* DEFAULT RANGE QUANT
	   Rast_get_fp_range_min_max(&fcb->fp_range, &dcell_min, &dcell_max);
	   if(!Rast_is_d_null_value(&dcell_min) && !Rast_is_d_null_value(&dcell_max))
	   {
	   Rast_get_range_min_max(&fcb->range, &cell_min, &cell_max);
	   Rast_quant_add_rule(&fcb->quant, dcell_min, dcell_max, 
	   cell_min, cell_max);
	   }
	*/
	Rast_quant_round(&fcb->quant);
	if (Rast_write_quant(fcb->name, fcb->mapset, &fcb->quant) < 0)
	    G_warning(_("unable to write quant file!"));
    }
    else {
	/* remove cell_misc/name/f_quant */
	G__file_name_misc(path, "cell_misc", QUANT_FILE, fcb->name,
			  fcb->mapset);
	remove(path);
    }

    /* create empty cats file */
    Rast_get_range_min_max(&fcb->range, &cell_min, &cell_max);
    if (Rast_is_c_null_value(&cell_max))
	cell_max = 0;
    Rast_init_cats((char *)NULL, &cats);
    Rast_write_cats(fcb->name, &cats);
    Rast_free_cats(&cats);

    /* write the histogram */
    /* only works for integer maps */
    if ((fcb->map_type == CELL_TYPE)
	&& (fcb->want_histogram)) {
	Rast_write_histogram_cs(fcb->name, &fcb->statf);
	Rast_free_cell_stats(&fcb->statf);
    }
    else {
	Rast_remove_histogram(fcb->name);
    }
}

static int close_new_gdal(int fd, int ok)
{
    struct fileinfo *fcb = &R__.fileinfo[fd];
    char path[GPATH_MAX];
    int stat = 1;

    if (ok) {
	int cell_fd;

	G_debug(1, "close %s GDAL", fcb->name);

	if (fcb->cur_row < fcb->cellhd.rows) {
	    int row;

	    Rast_zero_raster_buf(fcb->data, fcb->map_type);
	    for (row = fcb->cur_row; row < fcb->cellhd.rows; row++)
		Rast_put_raster_row(fd, fcb->data, fcb->map_type);
	    G_free(fcb->data);
	    fcb->data = NULL;
	}

	/* create path : full null file name */
	G__make_mapset_element_misc("cell_misc", fcb->name);
	G__file_name_misc(path, "cell_misc", NULL_FILE, fcb->name,
			  G_mapset());
	remove(path);

	/* write 0-length cell file */
	G__make_mapset_element("cell");
	G__file_name(path, "cell", fcb->name, fcb->mapset);
	cell_fd = creat(path, 0666);
	close(cell_fd);

	if (fcb->map_type != CELL_TYPE) {	/* floating point map */
	    if (write_fp_format(fd) != 0) {
		G_warning(_("Error writing floating point format file for map %s"),
			  fcb->name);
		stat = -1;
	    }

	    /* write 0-length fcell file */
	    G__make_mapset_element("fcell");
	    G__file_name(path, "fcell", fcb->name, fcb->mapset);
	    cell_fd = creat(path, 0666);
	    close(cell_fd);
	}
	else {
	    /* remove fcell/name file */
	    G__file_name(path, "fcell", fcb->name, fcb->mapset);
	    remove(path);
	    /* remove cell_misc/name/f_format */
	    G__file_name_misc(path, "cell_misc", FORMAT_FILE, fcb->name,
			      fcb->mapset);
	    remove(path);
	}

	if (Rast_close_gdal_write_link(fcb->gdal) < 0)
	    stat = -1;
    }
    else {
	remove(fcb->gdal->filename);
	Rast_close_gdal_link(fcb->gdal);
    }

    /* NOW CLOSE THE FILE DESCRIPTOR */
    close(fd);
    fcb->open_mode = -1;

    if (fcb->data != NULL)
	G_free(fcb->data);

    if (ok)
	write_support_files(fd);

    G_free(fcb->name);
    G_free(fcb->mapset);

    if (fcb->map_type != CELL_TYPE)
	Rast_quant_free(&fcb->quant);

    return stat;
}

static int close_new(int fd, int ok)
{
    struct fileinfo *fcb = &R__.fileinfo[fd];
    int stat;
    char path[GPATH_MAX];
    int row, i;
    const char *CELL_DIR;

    if (fcb->gdal)
	return close_new_gdal(fd, ok);

    if (ok) {
	switch (fcb->open_mode) {
	case OPEN_NEW_COMPRESSED:
	    G_debug(1, "close %s compressed", fcb->name);
	    break;
	case OPEN_NEW_UNCOMPRESSED:
	    G_debug(1, "close %s uncompressed", fcb->name);
	    break;
	}

	if (fcb->cur_row < fcb->cellhd.rows) {
	    Rast_zero_raster_buf(fcb->data, fcb->map_type);
	    for (row = fcb->cur_row; row < fcb->cellhd.rows; row++)
		Rast_put_raster_row(fd, fcb->data, fcb->map_type);
	    G_free(fcb->data);
	    fcb->data = NULL;
	}

	/* create path : full null file name */
	G__make_mapset_element_misc("cell_misc", fcb->name);
	G__file_name_misc(path, "cell_misc", NULL_FILE, fcb->name,
			  G_mapset());
	remove(path);

	if (fcb->null_cur_row > 0) {
	    /* if temporary NULL file exists, write it into cell_misc/name/null */
	    int null_fd;

	    null_fd = Rast__open_null_write(fd);
	    if (null_fd <= 0)
		return -1;
	    if (null_fd < 1)
		return -1;

	    /* first finish writing null file */
	    /* write out the rows stored in memory */
	    for (row = fcb->min_null_row; row < fcb->null_cur_row; row++)
		Rast__write_null_bits(null_fd,
				   fcb->NULL_ROWS[row - fcb->min_null_row],
				   row, fcb->cellhd.cols, fd);

	    /* write missing rows */
	    if (fcb->null_cur_row < fcb->cellhd.rows) {
		unsigned char *null_work_buf = Rast__allocate_null_bits(fcb->cellhd.cols);
		Rast__init_null_bits(null_work_buf, fcb->cellhd.cols);
		for (row = fcb->null_cur_row; row < fcb->cellhd.rows; row++)
		    Rast__write_null_bits(null_fd, null_work_buf, row,
				       fcb->cellhd.cols, fd);
		G_free(null_work_buf);
	    }
	    close(null_fd);

	    if (rename(fcb->null_temp_name, path)) {
		G_warning(_("Unable to renae null file '%s'"),
			  fcb->null_temp_name, path);
		stat = -1;
	    }
	    else {
		remove(fcb->null_temp_name);
	    }
	}
	else {
	    remove(fcb->null_temp_name);
	    remove(path);
	}			/* null_cur_row > 0 */

	if (fcb->open_mode == OPEN_NEW_COMPRESSED) {	/* auto compression */
	    fcb->row_ptr[fcb->cellhd.rows] = lseek(fd, 0L, SEEK_CUR);
	    Rast__write_row_ptrs(fd);
	}

	if (fcb->map_type != CELL_TYPE) {	/* floating point map */
	    int cell_fd;

	    if (write_fp_format(fd) != 0) {
		G_warning(_("Error writing floating point format file for map <%s>"),
			  fcb->name);
		stat = -1;
	    }

	    /* now write 0-length cell file */
	    G__make_mapset_element("cell");
	    cell_fd =
		creat(G__file_name(path, "cell", fcb->name, fcb->mapset),
		      0666);
	    close(cell_fd);
	    CELL_DIR = "fcell";
	}
	else {
	    /* remove fcell/name file */
	    G__file_name(path, "fcell", fcb->name, fcb->mapset);
	    remove(path);
	    /* remove cell_misc/name/f_format */
	    G__file_name_misc(path, "cell_misc", FORMAT_FILE, fcb->name,
			      fcb->mapset);
	    remove(path);
	    CELL_DIR = "cell";
	    close(fd);
	}
    }				/* ok */
    /* NOW CLOSE THE FILE DESCRIPTOR */

    close(fd);
    fcb->open_mode = -1;

    if (fcb->data != NULL)
	G_free(fcb->data);

    if (fcb->null_temp_name != NULL) {
	G_free(fcb->null_temp_name);
	fcb->null_temp_name = NULL;
    }

    /* if the cell file was written to a temporary file
     * move this temporary file into the cell file
     * if the move fails, tell the user, but go ahead and create
     * the support files
     */
    stat = 1;
    if (ok && (fcb->temp_name != NULL)) {
	G__file_name(path, CELL_DIR, fcb->name, fcb->mapset);
	remove(path);
	if (rename(fcb->temp_name, path)) {
	    G_warning(_("Unable to rename cell file '%s'"),
		      fcb->temp_name, path);
	    stat = -1;
	}
	else {
	    remove(fcb->temp_name);
	}
    }

    if (fcb->temp_name != NULL) {
	G_free(fcb->temp_name);
    }

    if (ok)
	write_support_files(fd);

    G_free(fcb->name);
    G_free(fcb->mapset);

    for (i = 0; i < NULL_ROWS_INMEM; i++)
	G_free(fcb->NULL_ROWS[i]);

    if (fcb->map_type != CELL_TYPE)
	Rast_quant_free(&fcb->quant);

    return stat;
}

/* returns 0 on success, 1 on failure */
static int write_fp_format(int fd)
{
    struct fileinfo *fcb = &R__.fileinfo[fd];
    struct Key_Value *format_kv;
    char path[GPATH_MAX];
    int stat;

    if (fcb->map_type == CELL_TYPE) {
	G_warning(_("unable to write f_format file for CELL maps"));
	return 0;
    }
    format_kv = G_create_key_value();
    if (fcb->map_type == FCELL_TYPE)
	G_set_key_value("type", "float", format_kv);
    else
	G_set_key_value("type", "double", format_kv);

    G_set_key_value("byte_order", "xdr", format_kv);

    if (fcb->open_mode == OPEN_NEW_COMPRESSED)
	G_set_key_value("lzw_compression_bits", "-1", format_kv);

    G__make_mapset_element_misc("cell_misc", fcb->name);
    G__file_name_misc(path, "cell_misc", FORMAT_FILE, fcb->name, fcb->mapset);
    G_write_key_value_file(path, format_kv, &stat);

    G_free_key_value(format_kv);

    return stat;
}
