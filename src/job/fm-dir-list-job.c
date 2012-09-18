/*
 *      fm-dir-list-job.c
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

/**
 * SECTION:fm-dir-list-job
 * @short_description: Job to get listing of directory.
 * @title: FmDirListJob
 *
 * @include: libfm/fm-dir-list-job.h
 *
 * The #FmDirListJob can be used to gather list of #FmFileInfo that some
 * directory contains.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "fm-dir-list-job.h"
#include <glib/gi18n-lib.h>
#include <gio/gio.h>
#include <string.h>
#include <glib/gstdio.h>
#include "fm-mime-type.h"
#include "fm-file-info-job.h"
#include "glib-compat.h"

#include "fm-file-info.h"

extern const char gfile_info_query_attribs[]; /* defined in fm-file-info-job.c */

enum {
    FILES_FOUND,
    N_SIGNALS
};

static void fm_dir_list_job_dispose              (GObject *object);
G_DEFINE_TYPE(FmDirListJob, fm_dir_list_job, FM_TYPE_JOB);

static int signals[N_SIGNALS];

static gboolean fm_dir_list_job_run(FmJob *job);
static void fm_dir_list_job_finished(FmJob* job);

static gboolean delay_add_files(gpointer user_data);

static void fm_dir_list_job_class_init(FmDirListJobClass *klass)
{
    GObjectClass *g_object_class;
    FmJobClass* job_class = FM_JOB_CLASS(klass);
    g_object_class = G_OBJECT_CLASS(klass);
    g_object_class->dispose = fm_dir_list_job_dispose;
    /* use finalize from parent class */

    job_class->run = fm_dir_list_job_run;
    job_class->finished = fm_dir_list_job_finished;

    /**
     * FmDirListJob::files-found
     * @job: a job that emitted the signal
     * @file: a FmFileInfo for the newly found file
     *
     * The #FmDirListJob::file-found signal is emitted for every file
     * found during directory listing. By default the signal is not
     * emitted for performance reason. This can be turned on by calling
     * fm_dir_list_job_set_emit_files_found().
     *
     * Return value: None
     *
     * Since: 0.1.2
     */
    signals[FILES_FOUND] =
        g_signal_new("files-found",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     G_STRUCT_OFFSET(FmDirListJobClass, files_found),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__POINTER,
                     G_TYPE_NONE, 1, G_TYPE_POINTER);

}


static void fm_dir_list_job_init(FmDirListJob *job)
{
    job->files = fm_file_info_list_new();
    fm_job_init_cancellable(FM_JOB(job));
}

/**
 * fm_dir_list_job_new
 * @path: path to directory to get listing
 * @dir_only: %TRUE to include only directories in the list
 *
 * Creates a new #FmDirListJob for directory listing. If @dir_only is
 * %TRUE then objects other than directories will be omitted from the
 * listing.
 *
 * Returns: (transfer full): a new #FmDirListJob object.
 *
 * Since: 0.1.0
 */
FmDirListJob* fm_dir_list_job_new(FmPath* path, gboolean dir_only)
{
    FmDirListJob* job = (FmDirListJob*)g_object_new(FM_TYPE_DIR_LIST_JOB, NULL);
    job->dir_path = fm_path_ref(path);
    job->dir_only = dir_only;
    return job;
}

/**
 * fm_dir_list_job_new_for_gfile
 * @gf: descriptor of directory to get listing
 *
 * Creates a new #FmDirListJob for listing of directory @gf.
 *
 * Returns: (transfer full): a new #FmDirListJob object.
 *
 * Since: 0.1.0
 */
FmDirListJob* fm_dir_list_job_new_for_gfile(GFile* gf)
{
    /* FIXME: should we cache this with hash table? Or, the cache
     * should be done at the level of FmFolder instead? */
    FmDirListJob* job = (FmDirListJob*)g_object_new(FM_TYPE_DIR_LIST_JOB, NULL);
    job->dir_path = fm_path_new_for_gfile(gf);
    return job;
}

static void fm_dir_list_job_dispose(GObject *object)
{
    FmDirListJob *job;

    g_return_if_fail(object != NULL);
    g_return_if_fail(FM_IS_DIR_LIST_JOB(object));

    job = (FmDirListJob*)object;

    if(job->dir_path)
    {
        fm_path_unref(job->dir_path);
        job->dir_path = NULL;
    }

    if(job->dir_fi)
    {
        fm_file_info_unref(job->dir_fi);
        job->dir_fi = NULL;
    }

    if(job->files)
    {
        fm_file_info_list_unref(job->files);
        job->files = NULL;
    }
    
    if(job->delay_add_files_handler)
    {
        g_source_remove(job->delay_add_files_handler);
        job->delay_add_files_handler = 0;
        g_slist_free_full(job->files_to_add, (GDestroyNotify)fm_file_info_unref);
        job->files_to_add = NULL;
    }

    if (G_OBJECT_CLASS(fm_dir_list_job_parent_class)->dispose)
        (* G_OBJECT_CLASS(fm_dir_list_job_parent_class)->dispose)(object);
}

static gboolean fm_dir_list_job_run_posix(FmDirListJob* job)
{
    FmJob* fmjob = FM_JOB(job);
    FmFileInfo* fi;
    GError *err = NULL;
    char* path_str;
    GDir* dir;

    path_str = fm_path_to_str(job->dir_path);

    fi = fm_file_info_new();
    fm_file_info_set_path(fi, job->dir_path);
    if( _fm_file_info_job_get_info_for_native_file(fmjob, fi, path_str, NULL) )
    {
        if(! fm_file_info_is_dir(fi))
        {
            err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("The specified directory is not valid"));
            fm_file_info_unref(fi);
            fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
            g_error_free(err);
            g_free(path_str);
            return FALSE;
        }
        job->dir_fi = fi;
    }
    else
    {
        err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("The specified directory is not valid"));
        fm_file_info_unref(fi);
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        g_error_free(err);
        g_free(path_str);
        return FALSE;
    }

    dir = g_dir_open(path_str, 0, &err);
    if( dir )
    {
        const char* name;
        GString* fpath = g_string_sized_new(4096);
        int dir_len = strlen(path_str);
        g_string_append_len(fpath, path_str, dir_len);
        if(fpath->str[dir_len-1] != '/')
        {
            g_string_append_c(fpath, '/');
            ++dir_len;
        }
        while( ! fm_job_is_cancelled(fmjob) && (name = g_dir_read_name(dir)) )
        {
            FmPath* new_path;
            g_string_truncate(fpath, dir_len);
            g_string_append(fpath, name);

            if(job->dir_only) /* if we only want directories */
            {
                struct stat st;
                /* FIXME: this results in an additional stat() call, which is inefficient */
                if(stat(fpath->str, &st) == -1 || !S_ISDIR(st.st_mode))
                    continue;
            }

            fi = fm_file_info_new();
            new_path = fm_path_new_child(job->dir_path, name);
            fm_file_info_set_path(fi, new_path);
            fm_path_unref(new_path);

        _retry:
            if( _fm_file_info_job_get_info_for_native_file(fmjob, fi, fpath->str, &err) )
                fm_dir_list_job_add_found_file(job, fi);
            else /* failed! */
            {
                FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MILD);
                g_error_free(err);
                err = NULL;
                if(act == FM_JOB_RETRY)
                    goto _retry;
            }
            fm_file_info_unref(fi);
        }
        g_string_free(fpath, TRUE);
        g_dir_close(dir);
    }
    else
    {
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        g_error_free(err);
    }
    g_free(path_str);
    return TRUE;
}

static gboolean fm_dir_list_job_run_gio(FmDirListJob* job)
{
    GFileEnumerator *enu;
    GFileInfo *inf;
    FmFileInfo* fi;
    GError *err = NULL;
    FmJob* fmjob = FM_JOB(job);
    GFile* gf;
    const char* query;

    gf = fm_path_to_gfile(job->dir_path);
_retry:
    inf = g_file_query_info(gf, gfile_info_query_attribs, 0, fm_job_get_cancellable(fmjob), &err);
    if(!inf )
    {
        FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MODERATE);
        g_error_free(err);
        if( act == FM_JOB_RETRY )
        {
            err = NULL;
            goto _retry;
        }
        else
        {
            g_object_unref(gf);
            return FALSE;
        }
    }

    if( g_file_info_get_file_type(inf) != G_FILE_TYPE_DIRECTORY)
    {
        err = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_DIRECTORY, _("The specified directory is not valid"));
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        g_error_free(err);
        g_object_unref(gf);
        g_object_unref(inf);
        return FALSE;
    }

    /* FIXME: should we use fm_file_info_new + fm_file_info_set_from_gfileinfo? */
    job->dir_fi = fm_file_info_new_from_gfileinfo(job->dir_path, inf);
    g_object_unref(inf);

    if(G_UNLIKELY(job->dir_only))
    {
        query = G_FILE_ATTRIBUTE_STANDARD_TYPE","G_FILE_ATTRIBUTE_STANDARD_NAME","
                G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN","G_FILE_ATTRIBUTE_STANDARD_IS_BACKUP","
                G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK","G_FILE_ATTRIBUTE_STANDARD_IS_VIRTUAL","
                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME","G_FILE_ATTRIBUTE_STANDARD_ICON","
                G_FILE_ATTRIBUTE_STANDARD_SIZE","G_FILE_ATTRIBUTE_STANDARD_TARGET_URI","
                "unix::*,time::*,access::*,id::filesystem";
    }
    else
        query = gfile_info_query_attribs;

    enu = g_file_enumerate_children (gf, query, 0, fm_job_get_cancellable(fmjob), &err);
    g_object_unref(gf);
    if(enu)
    {
        while( ! fm_job_is_cancelled(fmjob) )
        {
            inf = g_file_enumerator_next_file(enu, fm_job_get_cancellable(fmjob), &err);
            if(inf)
            {
                FmPath *dir, *sub;
                if(G_UNLIKELY(job->dir_only))
                {
                    /* FIXME: handle symlinks */
                    if(g_file_info_get_file_type(inf) != G_FILE_TYPE_DIRECTORY)
                    {
                        g_object_unref(inf);
                        continue;
                    }
                }

                /* virtual folders may return childs not within them */
                dir = fm_path_new_for_gfile(g_file_enumerator_get_container(enu));
                sub = fm_path_new_child(dir, g_file_info_get_name(inf));
                fi = fm_file_info_new_from_gfileinfo(sub, inf);
                fm_path_unref(sub);
                fm_path_unref(dir);
                fm_dir_list_job_add_found_file(job, fi);
                fm_file_info_unref(fi);
            }
            else
            {
                if(err)
                {
                    FmJobErrorAction act = fm_job_emit_error(fmjob, err, FM_JOB_ERROR_MILD);
                    g_error_free(err);
                    /* FM_JOB_RETRY is not supported. */
                    if(act == FM_JOB_ABORT)
                        fm_job_cancel(fmjob);
                }
                break; /* FIXME: error handling */
            }
            g_object_unref(inf);
        }
        g_file_enumerator_close(enu, NULL, &err);
        g_object_unref(enu);
    }
    else
    {
        fm_job_emit_error(fmjob, err, FM_JOB_ERROR_CRITICAL);
        g_error_free(err);
        return FALSE;
    }
    return TRUE;
}

static gboolean fm_dir_list_job_run(FmJob* fmjob)
{
    gboolean ret;
    FmDirListJob* job = FM_DIR_LIST_JOB(fmjob);
    g_return_val_if_fail(job->dir_path != NULL, FALSE);
    if(fm_path_is_native(job->dir_path)) /* if this is a native file on real file system */
        ret = fm_dir_list_job_run_posix(job);
    else /* this is a virtual path or remote file system path */
        ret = fm_dir_list_job_run_gio(job);
    return ret;
}

static void fm_dir_list_job_finished(FmJob* job)
{
    FmDirListJob* dirlist_job = FM_DIR_LIST_JOB(job);
    FmJobClass* job_class = FM_JOB_CLASS(fm_dir_list_job_parent_class);

    if(dirlist_job->emit_files_found)
    {
        if(dirlist_job->delay_add_files_handler)
        {
            g_source_remove(dirlist_job->delay_add_files_handler);
            delay_add_files(dirlist_job);
        }
    }
    if(job_class->finished)
        job_class->finished(job);
}


/**
 * fm_dir_list_job_get_dir_path
 * @job: the job that collected listing
 *
 * Retrieves the path of the directory being listed.
 *
 * Returns: (transfer none): FmPath for the directory.
 *
 * Since: 1.0.2
 */
FmPath* fm_dir_list_job_get_dir_path(FmDirListJob* job)
{
    return job->dir_path;
}

/**
 * fm_dir_list_job_get_dir_info
 * @job: the job that collected listing
 *
 * Retrieves the information of the directory being listed.
 *
 * Returns: (transfer none): FmFileInfo for the directory.
 *
 * Since: 1.0.2
 */
FmFileInfo* fm_dir_list_job_get_dir_info(FmDirListJob* job)
{
    return job->dir_fi;
}

/**
 * fm_dir_list_job_get_files
 * @job: the job that collected listing
 *
 * Retrieves gathered listing from the @job. This function may be called
 * only from #FmJob::finished signal handler. Returned data is owned by
 * the @job and should be not freed by caller.
 *
 * Before 1.0.1 this call had name fm_dir_dist_job_get_files due to typo.
 *
 * Returns: (transfer none): list of gathered data.
 *
 * Since: 0.1.0
 */
FmFileInfoList* fm_dir_list_job_get_files(FmDirListJob* job)
{
    return job->files;
}

#ifndef FM_DISABLE_DEPRECATED
/**
 * fm_dir_dist_job_get_files
 * @job: the job that collected listing
 *
 * There is a typo in the function name. It should have been 
 * fm_dir_list_job_get_files(). The one with typo is kept here for backward
 * compatibility and will be removed later.
 *
 * Since: 0.1.0
 *
 * Deprecated: 1.0.1: Use fm_dir_list_job_get_files() instead.
 */
FmFileInfoList* fm_dir_dist_job_get_files(FmDirListJob* job)
{
    return fm_dir_list_job_get_files(job);
}
#endif /* FM_DISABLE_DEPRECATED */

void fm_dir_list_job_set_emit_files_found(FmDirListJob* job, gboolean emit_files_found)
{
    job->emit_files_found = emit_files_found;
}

gboolean fm_dir_list_job_get_emit_files_found(FmDirListJob* job)
{
    return job->emit_files_found;
}

static gboolean delay_add_files(gpointer user_data)
{
    /* this callback is called from the main thread */
    FmDirListJob* job = FM_DIR_LIST_JOB(user_data);
    /* g_print("delay_add_files: %d\n", g_slist_length(job->files_to_add)); */

    g_signal_emit(job, signals[FILES_FOUND], 0, job->files_to_add);
    g_slist_free_full(job->files_to_add, (GDestroyNotify)fm_file_info_unref);
    job->files_to_add = NULL;
    job->delay_add_files_handler = 0;
    return FALSE;
}

static gpointer queue_add_file(FmJob* fmjob, gpointer user_data)
{
    FmDirListJob* job = FM_DIR_LIST_JOB(fmjob);
    FmFileInfo* file = FM_FILE_INFO(user_data);
    /* this callback is called from the main thread */
    /* g_print("queue_add_file: %s\n", fm_file_info_get_disp_name(file)); */
    job->files_to_add = g_slist_prepend(job->files_to_add, fm_file_info_ref(file));
    if(job->delay_add_files_handler == 0)
        job->delay_add_files_handler = g_timeout_add_seconds_full(G_PRIORITY_LOW, 1, delay_add_files, g_object_ref(job), g_object_unref);
    return NULL;
}

/**
 * fm_dir_dist_job_add_found_file
 * @job: the job that collected listing
 * @file: a FmFileInfo of the newly found file
 *
 * This API is called by the implementation of FmDirListJob only.
 * Application developers should not use this API.
 * When a new file is found in the dir being listed, implementations
 * of FmDirListJob should call this API with the info of the newly found
 * file. The FmFileInfo will be added to the found file list.
 * 
 * If emission of "files-found" signal is turned on by 
 * fm_dir_list_job_set_emit_files_found(), a "files-found" signal is emitted
 * for the newly found files after several new files are added.
 * See the document for "files-found" signal for more detail.
 *
 * Since: 1.0.2
 */
void fm_dir_list_job_add_found_file(FmDirListJob* job, FmFileInfo* file)
{
    fm_file_info_list_push_tail(job->files, file);
    if(G_UNLIKELY(job->emit_files_found))
        fm_job_call_main_thread(FM_JOB(job), queue_add_file, file);
}

/**
 * fm_dir_list_job_set_dir_path
 * @job: the job that collected listing
 * @path: a FmPath of the directory being loaded.
 *
 * This API is called by the implementation of FmDirListJob only.
 * Application developers should not use this API most of the time.
 *
 * Since: 1.0.2
 */
void fm_dir_list_job_set_dir_path(FmDirListJob* job, FmPath* path)
{
    if(job->dir_path)
        fm_path_unref(job->dir_path);
    job->dir_path = fm_path_ref(path);
}

/**
 * fm_dir_list_job_set_dir_info
 * @job: the job that collected listing
 * @info: a FmFileInfo of the directory being loaded.
 *
 * This API is called by the implementation of FmDirListJob only.
 * Application developers should not use this API most of the time.
 *
 * Since: 1.0.2
 */
void fm_dir_list_job_set_dir_info(FmDirListJob* job, FmFileInfo* info)
{
    if(job->dir_fi)
        fm_file_info_unref(job->dir_fi);
    job->dir_fi = fm_file_info_ref(info);
}
