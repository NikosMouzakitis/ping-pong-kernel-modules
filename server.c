#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <linux/signal.h>
#include <linux/sched/signal.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/inet_connection_sock.h>
#include <net/request_sock.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/kdev_t.h>
#include <uapi/linux/stat.h>
#include <linux/debugfs.h>
#include <linux/types.h>

MODULE_LICENSE("GPL");

#define DEFAULT_PORT	3222
#define MODULE_NAME	"tserver"
#define MAX_CONS	16
#define BUF_SIZE	1024
#define EXNORM		0x1

//Data structure used for
//reception of a tcp msg.

struct dst {
	struct sockaddr_in cl_client;
	int cl_len;
	struct task_struct *thread;
};

struct proc_dir_entry *ent;
static int serv_sock_alive = 0;
static int sthread_running = 0;
static char pin[BUF_SIZE];
struct dst *base;
struct socket *serv_sock;
struct sockaddr_in *address;
int normal_exit;
const char *run_command = "server run";
const char *server_default_reply = "success";
const char buffer[1024];
int pkt;

int reply_testing(struct socket *sock, int *pkt)
{
	struct msghdr msg;
	struct kvec vec;
	int written = 0;
	int flags, left, error;
	unsigned long int LEN;
	struct iovec iov;

	// increasing the received packet counter.
	*pkt = (*pkt) + 1;

	flags = 0;

	LEN = sizeof(int);

//	iov.iov_base = (void *)server_default_reply;
	iov.iov_base = (void *)pkt;
//	iov.iov_len = (__kernel_size_t)strlen(server_default_reply);;
	iov.iov_len = (__kernel_size_t)LEN;

	if( sock == NULL)
		return -1;

	//to define LEN

	msg.msg_name = 0;	
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	iov_iter_init( &msg.msg_iter, WRITE, &iov, 1, LEN);

	//vec.iov_len = strlen(server_default_reply) * sizeof(char);
	left = vec.iov_len;
	left = iov.iov_len;
	//printk(KERN_ALERT "Transmition characters lenght: %ld\n", vec.iov_len);
repeat_send:
	//vec.iov_base = (char *)&server_default_reply[ written];
	vec.iov_base = (char *)&pkt[ written];
	
//	error = sock->ops->sendmsg(sock, (struct msghdr*)&msg,left);
	
	error = tcp_sendmsg(sock->sk, &msg, left);

	if(error < 0) {
		printk("Error in send: %d\n", error);
	}	
//	printk("SERVER succesfull TX\n");

	if( (error == -ERESTARTSYS) || (!(flags & MSG_DONTWAIT) && (error == -EAGAIN)) ) 
		goto repeat_send;

	if(error > 0) {
		written += error;
		left -= error;
	
		if(left)
			goto repeat_send;
	}
	return (written ? written : error);
}

char *inet_ntoa(struct in_addr *in) 
{
	char *str_ip = NULL;

	unsigned int int_ip = 0;
	str_ip = kmalloc(16*sizeof(char), GFP_KERNEL);
	
	if(str_ip == NULL)
		return NULL;
	else
		memset(str_ip, 0, 16);

	int_ip = in->s_addr;
	sprintf(str_ip, "%d.%d.%d.%d", (int_ip) & 0xFF, (int_ip >>8) & 0xFF, (int_ip >> 16) & 0xFF,(int_ip >> 24) & 0xFF);

	return str_ip;
}

size_t RcvBuf( struct socket *sock, const char *Buffer, size_t Length)
{
	struct msghdr msg;
	struct sk_buff *tmpsk;
	struct kvec vec;
	int len;
	int kflag;
	struct iovec iov;
	len = 0;
	kflag = 0;	

	if(sock == NULL) {
		printk("FATAL error NULL socket.\n");	
		return -1;
	}
	
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;
	vec.iov_base = (char *)Buffer;
	vec.iov_len = (size_t) Length;

	iov.iov_base = (char *)Buffer;
	iov.iov_len = (size_t)Length;

read_again:

	if(!skb_queue_empty(&sock->sk->sk_receive_queue))
		pr_info("[server] recv queue empty?  %s \n", skb_queue_empty(&sock->sk->sk_receive_queue) ? "yes" : "no");

//	len = kernel_recvmsg(sock, &msg, &vec, Length, Length, kflag);

	//	Reception via tcp_recvmsg() function.
	//***************************************************//
	
	//setting the parameters for tcp_recvmsg().	
	int nonblock = 0; // blocking.

	// nicko -->LEAVE IT ZERO (?) , works for now.
	int *addr_len = 0 ;
	// nicko -->IMPORTANT:: in iov.iov_base is where the data will be actually received.
	iov_iter_init( &msg.msg_iter, READ, &iov, 1, 4);
	len = tcp_recvmsg(sock->sk, &msg, Length, nonblock, kflag, addr_len);
	
	//***************************************************//

	if(len == -EAGAIN || len == -ERESTARTSYS)
		goto read_again;

//	printk(KERN_INFO "SERVER succesfull RX\n");
	
	//saving value in the pkt integer.	
	pkt = *Buffer;
	
	return len;

}

struct socket * server_accept_conn(struct socket *sock)
{
	struct socket * newsock;
	struct inet_connection_sock *isock;
	int error;
	char *tmp;
	
	DECLARE_WAITQUEUE(accept_wait, current);
	allow_signal(SIGKILL);
	allow_signal(SIGSTOP);

	// sock_create
	error = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &newsock);
	
	if(error) {
		printk(KERN_ALERT "Error during creation of newsock.Terminating\n");
		return NULL;
	}


	newsock->type = sock->type;
	newsock->ops = sock->ops;
	
	tcp_init_sock(newsock->sk);

	//isock addition, in goal to make accept non-blocking.
	isock = inet_csk(sock->sk);

	add_wait_queue(&sock->sk->sk_wq->wait, &accept_wait);
	

	while(reqsk_queue_empty(&isock->icsk_accept_queue)) {
		__set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);

		if(kthread_should_stop()) {
			printk("thread stopping execution");
			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&sock->sk->sk_wq->wait, &accept_wait);
			printk("sock_release() on newsock");
			sock_release(newsock);
			normal_exit = EXNORM;
			return NULL;
		}
		
		if(signal_pending(current)) {
			__set_current_state(TASK_RUNNING);
			remove_wait_queue(&sock->sk->sk_wq->wait, &accept_wait);
			goto release;
		}
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&sock->sk->sk_wq->wait, &accept_wait);

	// accepting
	printk("Accept a connection\n");
	
	error = newsock->ops->accept(sock, newsock, O_NONBLOCK,1);

	// if error cleaning the newsock created.	
	if(error) {
		printk(KERN_ALERT "Error acceptin' socket\n");
		goto release;
	}

	/// getting name	
	error = newsock->ops->getname(newsock, (struct sockaddr *) &(base->cl_client),&(base->cl_len), 2);


	printk("len addr: %d\n", (base->cl_len));

	if(error < 0) {
		printk(KERN_ALERT "Error in getting name\n");
		return NULL;
	}

	tmp = inet_ntoa( &(base->cl_client.sin_addr));

	printk(KERN_INFO "Connection from %s : %d\n",tmp, ntohs(base->cl_client.sin_port));

	return newsock;

release:
	sock_release(newsock);
	printk("sock_release() on newsock;");
	return NULL;
}


struct socket * set_up_server_socket(int port_no)
{
	struct socket *sock;
	struct sockaddr_in sin;
	int error;

	// Structure allocation.
	base = kmalloc(sizeof(struct dst), GFP_KERNEL);

	if(base == NULL) {
	
		printk(KERN_ALERT "Kmalloc error\n");

		return NULL;
	}
	
	memset(base, 0, sizeof(struct dst));
	
	base->cl_len = sizeof(struct sockaddr_in);	

	error = sock_create(AF_INET, SOCK_STREAM, IPPROTO_TCP, &sock);

	if(error<0) {
		printk(KERN_ALERT  "Error creating socket, terminating\n");
		return NULL;
	}
	printk(KERN_INFO  "Socket created\n");
	
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port_no);

	error = sock->ops->bind(sock, (struct sockaddr*)&sin, sizeof(sin));
	
	if(error) {

		printk(KERN_ALERT "Error binding socket: %d\n", error);
		return NULL;
	}
	
	printk(KERN_INFO "Bind success.\n");

	error = sock->ops->listen(sock, 16);

	if(error != 0 ) {
		printk(KERN_ALERT "Error listening socket\n");
		return NULL;
	}

	return sock;
}

// a string compare implementation -1:not equal , 0:equal.
static int my_strcmp( char * one,const char * two) 
{
	int len1, len2,i;
	len1 = len2 = 0;

	for( i = 0; one[i] != '\0'; i++)
		len1++;

	for( i = 0; two[i] != '\0'; i++)
		len2++;

	if(len1 != len2)
		return -1;

	for(i = 0; i < len1; i++)
		if(one[i] != two[i])
			return -1;

	return 0; // string equality.
}

void  server_thread(void) {
	int len;
	int send_rv;
	struct socket *test_socket;
	int times = 0;
	normal_exit = 0;	

	while(!kthread_should_stop() ) {
	
		test_socket = server_accept_conn(serv_sock);

		if(test_socket == NULL) {

			if( normal_exit != EXNORM)	
				printk(KERN_ALERT "Error in server_accept_conn()\n");

			return;

		} else {
			
			printk(KERN_INFO "Accepted a connection.\n");

		}
	
		 
		while(	times < 3) {
			len = RcvBuf(test_socket, buffer, 4);
			printk(KERN_INFO "Server Received: %d\n", pkt);
			send_rv = reply_testing(test_socket,&pkt);	
			printk("Server send: %d\n", pkt);	
			times++;
		}	
		printk("SERVER releases the accepted socket.\n");	
		sock_release(test_socket);
		
	}
	printk("Kthread is terminating\n");

	return;
}


static ssize_t mwrite(struct file *file, const char __user *ubuf, size_t count, loff_t *offset)
{
	int rv;
	char __user *p = (char *) ubuf;

	printk("Write handler\n");
	printk("size: %d offset: %d\n", (int) count, (int) *offset);

	if(count > BUF_SIZE) 
		return -EFAULT;

	rv = copy_from_user(pin, p, count);

	printk("Module received ::[.write]: %s\n", pin);

	/*	Check for command to start the server thread. */

	if( (my_strcmp(pin, run_command) == -1) && (serv_sock_alive == 0)) {

		printk("Starting the server thread.\n");	

		// creating a listening socket in server module.
		serv_sock = set_up_server_socket(DEFAULT_PORT);
	
		if(serv_sock == NULL) {
			printk( KERN_ALERT "Abnormal termination\n");
			return -1;
		}	

		//reuse socket.
		serv_sock->sk->sk_reuse = 1;
		serv_sock_alive = 1;	//flag for cleanup.
		//running the thread.	
		base->thread = kthread_run( (void *) server_thread, NULL, MODULE_NAME);
		printk("created kthread.\n");
		sthread_running++;	
	}

	if( (rv > 0) && (rv < count) ) {
		printk("Partial write\n");
	}
	return count;
}

static struct file_operations fops = 
{
	.owner = THIS_MODULE,
	.write = mwrite,
};

static void module_exit0(void)
{
	
	//	stopping the kernel thread.
	if(sthread_running) {
		int ret = kthread_stop(base->thread);
		if(ret != -EINTR)
			printk("Kthread has stopped\n");
	}

	// free-ing the base allocated memory.	
	if(serv_sock_alive) {

		sock_release(serv_sock);
		kfree(base);
		printk("released serv_sock: Module exit\n");
	}
	
	proc_remove(ent);
	printk(KERN_INFO " -- Server Module removed\n");

}

static int module_init0(void)
{

	ent = proc_create("server_dev", 0660, NULL, &fops);
	memset((void *)buffer, 0, 1024);
	printk(KERN_INFO "--Server Module loaded into kernel.\n");
	return (0);
}
module_init(module_init0);
module_exit(module_exit0);
