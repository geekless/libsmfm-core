/*
 *      fm-dir-list-job.h
 *      
 *      Copyright 2009 PCMan <pcman.tw@gmail.com>
 *      
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *      
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *      
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */


#ifndef __FM_DIR_LIST_JOB_H__
#define __FM_DIR_LIST_JOB_H__

#include "fm-job.h"
#include "fm-path.h"
#include "fm-file-info.h"

G_BEGIN_DECLS

#define FM_TYPE_DIR_LIST_JOB				(fm_dir_list_job_get_type())
#define FM_DIR_LIST_JOB(obj)				(G_TYPE_CHECK_INSTANCE_CAST((obj),\
			FM_TYPE_DIR_LIST_JOB, FmDirListJob))
#define FM_DIR_LIST_JOB_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass),\
			FM_TYPE_DIR_LIST_JOB, FmDirListJobClass))
#define FM_IS_DIR_LIST_JOB(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj),\
			FM_TYPE_DIR_LIST_JOB))
#define FM_IS_DIR_LIST_JOB_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),\
			FM_TYPE_DIR_LIST_JOB))

typedef struct _FmDirListJob            FmDirListJob;
typedef struct _FmDirListJobClass       FmDirListJobClass;

/**
 * FmDirListJob
 * @parent: the parent object
 * @dir_path: directory to get listing
 * @dir_only: %TRUE if list only subdirectories
 * @dir_fi: file info of the directory
 * @files: the listing of the directory
 */
struct _FmDirListJob
{
    /* FIXME: should seal this all */
    /*< public >*/
    FmJob parent;
    FmPath* dir_path;
    gboolean dir_only;
    FmFileInfo* dir_fi;
    FmFileInfoList* files;
    /*< private >*/
    gboolean emit_files_found;
    guint delay_add_files_handler;
    GSList* files_to_add;
};

struct _FmDirListJobClass
{
    /*< private >*/
    FmJobClass parent_class;

    /* signals */
    void (*files_found)(FmDirListJob* job, GSList* files);
};

GType           fm_dir_list_job_get_type(void);
FmDirListJob*   fm_dir_list_job_new(FmPath* path, gboolean dir_only);
FmDirListJob*   fm_dir_list_job_new_for_gfile(GFile* gf);
FmFileInfoList* fm_dir_list_job_get_files(FmDirListJob* job);
void            fm_dir_list_job_set_incremental(FmDirListJob* job, gboolean set);

/*
FmPath* fm_dir_list_job_get_dir_path(FmDirListJob* job);
FmFileInfo* fm_dir_list_job_get_dir_info(FmDirListJob* job);
void fm_dir_list_job_set_dir_path(FmDirListJob* job, FmPath* path);
void fm_dir_list_job_set_dir_info(FmDirListJob* job, FmFileInfo* info);

void fm_dir_list_job_set_emit_files_found(FmDirListJob* job, gboolean emit_files_found);
gboolean fm_dir_list_job_get_emit_files_found(FmDirListJob* job);
*/
void fm_dir_list_job_add_found_file(FmDirListJob* job, FmFileInfo* file);

G_END_DECLS

#endif /* __FM-DIR-LIST-JOB_H__ */
