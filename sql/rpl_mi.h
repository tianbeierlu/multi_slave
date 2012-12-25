/* Copyright (c) 2006, 2012, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef RPL_MI_H
#define RPL_MI_H

#ifdef HAVE_REPLICATION

#include "rpl_rli.h"
#include "rpl_reporting.h"
#include "my_sys.h"

typedef struct st_mysql MYSQL;

/*****************************************************************************
  Replication IO Thread

  Master_info contains:
    - information about how to connect to a master
    - current master log name
    - current master log offset
    - misc control variables

  Master_info is initialized once from the master.info file if such
  exists. Otherwise, data members corresponding to master.info fields
  are initialized with defaults specified by master-* options. The
  initialization is done through init_master_info() call.

  The format of master.info file:

  log_name
  log_pos
  master_host
  master_user
  master_pass
  master_port
  master_connect_retry

  To write out the contents of master.info file to disk ( needed every
  time we read and queue data from the master ), a call to
  flush_master_info() is required.

  To clean up, call end_master_info()

*****************************************************************************/

class Master_info : public Slave_reporting_capability
{
 public:
  Master_info(bool is_slave_recovery);
  ~Master_info();
  bool shall_ignore_server_id(ulong s_id);
  void clear_in_memory_info(bool all);

  /* the variables below are needed because we can change masters on the fly */
  char master_log_name[FN_REFLEN];
  char host[HOSTNAME_LENGTH+1];
  char user[USERNAME_LENGTH+1];
  char password[MAX_PASSWORD_LENGTH+1];
  my_bool ssl; // enables use of SSL connection if true
  char ssl_ca[FN_REFLEN], ssl_capath[FN_REFLEN], ssl_cert[FN_REFLEN];
  char ssl_cipher[FN_REFLEN], ssl_key[FN_REFLEN];
  my_bool ssl_verify_server_cert;

  my_off_t master_log_pos;
  File fd; // we keep the file open, so we need to remember the file pointer
  IO_CACHE file;

  mysql_mutex_t data_lock, run_lock, sleep_lock;
  mysql_cond_t data_cond, start_cond, stop_cond, sleep_cond;
  THD *io_thd;
  MYSQL* mysql;
  uint32 file_id;				/* for 3.23 load data infile */
  Relay_log_info rli;
  uint port;
  uint connect_retry;
#ifndef DBUG_OFF
  int events_till_disconnect;
#endif
  bool inited;
  volatile bool abort_slave;
  volatile uint slave_running;
  volatile ulong slave_run_id;
  /*
     The difference in seconds between the clock of the master and the clock of
     the slave (second - first). It must be signed as it may be <0 or >0.
     clock_diff_with_master is computed when the I/O thread starts; for this the
     I/O thread does a SELECT UNIX_TIMESTAMP() on the master.
     "how late the slave is compared to the master" is computed like this:
     clock_of_slave - last_timestamp_executed_by_SQL_thread - clock_diff_with_master

  */
  long clock_diff_with_master;
  /*
    Keeps track of the number of events before fsyncing.
    The option --sync-master-info determines how many
    events should happen before fsyncing.
  */
  uint sync_counter;
  float heartbeat_period;         // interface with CHANGE MASTER or master.info
  ulonglong received_heartbeats;  // counter of received heartbeat events
  DYNAMIC_ARRAY ignore_server_ids;
  ulong master_id;
};

typedef struct msev_info_struct msev_info_t;
struct msev_info_struct
{
	char							curr_evpath[FN_REFLEN];//存储当前事件(组)所在的relay-log文件路径
	my_off_t						ev_pos;
};

typedef struct msti_error_info_struct msti_error_info_t;
struct msti_error_info_struct
{
	uint							current_index;
	uint							group_counts;
	uint							array_len;
	msev_info_t**					group_info;
};

typedef struct multi_slave_thread_info_struct multi_slave_thread_info_t;
struct multi_slave_thread_info_struct
{
	THD*							exec_thd;
	Relay_log_info*					exec_rli;

	uint							id;
	//指向要执行的事件链表的头和尾
	my_lst_t						ev_lst;

	mysql_mutex_t					ev_mutex;

	mysql_cond_t					ev_cond;
	mysql_mutex_t					ev_lock;

	pthread_t						thread_id;

	msev_info_t*					current_grpinfo;/*当前正在复制的组信息*/
};

typedef struct multi_slave_struct multi_slave_t;
struct multi_slave_struct
{
	multi_slave_thread_info_t**		msti_array;/*管理多个线程*/
	uint							last_thd_index;/*用来表示当前事件的线程下标，如果没有任何事务开始，则这个默认为0的
												   不属于任何事务的事件都是通过线程0来执行的，当有事务开始后，才需要分发*/
	volatile uint					status;
	mysql_mutex_t					thread_mutex;
	uint							inited_threads;	/*用来表示成功初始化的线程数*/

	uint							error_process;	/*用来控制错误处理的，如果有一个线程出错了，如果这个值为假，则先将
													msti.log文件删除，然后再新写这个文件，如果同时又有一个线程出错了，
													那么此时已经为真，则直接往后追加即可*/

	my_lst_t						ev_lst;/*在没有确定要分发到某一个线程之前，将所有的事件都放到这个临时链表中*/

	msev_info_t*					group_info;
	
	uint							trans_flag;
	File							error_fd;
	IO_CACHE*						error_iocache;
#if multi_test
	uint*							count;
	msev_info_t*					tableinfo;
#endif
};

#define MSTI_ERROR_ARRAY_LEN			100
#define MSTI_ERROR_ARRAY_INC_STEP		100

int init_master_info(Master_info* mi, const char* master_info_fname,
		     const char* slave_info_fname,
		     bool abort_if_no_master_info_file,
		     int thread_mask);
void end_master_info(Master_info* mi);
int flush_master_info(Master_info* mi, 
                      bool flush_relay_log_cache, 
                      bool need_lock_relay_log);
int change_master_server_id_cmp(ulong *id1, ulong *id2);

#endif /* HAVE_REPLICATION */
#endif /* RPL_MI_H */
