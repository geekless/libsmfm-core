/*
 *      fm-job.h
 *      
 *      Copyright 2009 PCMan <pcman@thinkpad>
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


#ifndef __FM_JOB_H__
#define __FM_JOB_H__

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define FM_TYPE_JOB				(fm_job_get_type())
#define FM_JOB(obj)				(G_TYPE_CHECK_INSTANCE_CAST((obj),\
			FM_TYPE_JOB, FmJob))
#define FM_JOB_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass),\
			FM_TYPE_JOB, FmJobClass))
#define FM_IS_JOB(obj)			(G_TYPE_CHECK_INSTANCE_TYPE((obj),\
			FM_TYPE_JOB))
#define FM_IS_JOB_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass),\
			FM_TYPE_JOB))

typedef struct _FmJob			FmJob;
typedef struct _FmJobClass		FmJobClass;

typedef gpointer (*FmJobCallMainThreadFunc)(FmJob* job, gpointer user_data);

struct _FmJob
{
	GObject parent;
	gboolean cancel;

	/* optional, should be created if the job uses gio */
	GCancellable* cancellable;

	/* optional, used when blocking the job to call a callback in 
	 * main thread is needed. */
	GMutex* mutex;
	GCond* cond;
	/* GSList* async_calls; */
};

struct _FmJobClass
{
	GObjectClass parent_class;

	void (*finished)(FmJob* job);
	void (*error)(FmJob* job);
	void (*cancelled)(FmJob* job);
	gint (*ask)(FmJob* job, const char* question, gint options);

	gboolean (*run)(FmJob* job);
	gboolean (*run_sync)(FmJob* job);
	void (*cancel)(FmJob* job);
};

GType	fm_job_get_type		(void);

/* base type of all file I/O jobs.
 * not directly called by applications. */
FmJob*	fm_job_new			(void);

/* run an job asynchronously in another working thread, and 
 * emit 'finished' signal in the main thread on its termination.
 * the default implementation of FmJob::run() create a working
 * thread, and calls fm_job_run_sync in it.
 */
gboolean fm_job_run(FmJob* job);

/* run a job in current thread in a blocking fashion.  */
gboolean fm_job_run_sync(FmJob* job);

/* cancel the running job. */
void fm_job_cancel(FmJob* job);

/* Following APIs are private to FmJob and should only be used in the
 * implementation of classes derived from FmJob.
 * Besides, they should be called from working thread only */

gpointer fm_job_call_main_thread(FmJob* job, 
					FmJobCallMainThreadFunc func, gpointer user_data);

/* gpointer fm_job_call_main_thread_async(FmJob* job, GFunc func, gpointer user_data); */

/* Used to implement FmJob::run() using gio inside.
 * This API tried to initialize a GCancellable object for use with gio.
 * If this function returns FALSE, that means the job is already 
 * cancelled before the cancellable object is created.
 * So the following I/O operations should be cancelled. */
gboolean fm_job_init_cancellable(FmJob* job);

void fm_job_finish(FmJob* job);

void fm_job_emit_finished(FmJob* job);

void fm_job_emit_cancelled(FmJob* job);

gint fm_job_ask(FmJob* job, const char* question, gint options);

G_END_DECLS

#endif /* __FM-JOB_H__ */
