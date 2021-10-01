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

#define MODULE_NAME	"tclient"
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
static char pin[BUF_SIZE];
struct dst *base;
struct socket *serv_sock;
struct sockaddr_in *address;
int normal_exit;

//command to trigger the kenrel ping pong.
const char *run_command = "ping pong";
const char buffer[1024];
const char *client_test = "alive";

// function responsible for ping-pong between the two modules.
static void ping_pong_proc(void)
{
	struct msghdr msg;
	int written = 0;
	int flags, left, error, LEN;
	struct iovec iov;
	char ch;
	int len , kflag = 0;
	int nonblock;
	int *addr_len;
	int pingcount = 0;
	int transmit_val = 3;

	char Buffer[1024];
	char sendBuf[1024];

	struct msghdr rmsg;
	struct kvec rvec;

	ch = 'a';
	flags = 0;
	memset((void *)sendBuf, 0, 1024);	
	memset((void *)Buffer, 0, 1024);	

	// responsible for reception
	rmsg.msg_name = 0;
	rmsg.msg_namelen = 0;
	rmsg.msg_control = NULL;
	rmsg.msg_controllen = 0;
	rmsg.msg_flags = 0;

	rvec.iov_base = (char *)Buffer;
	rvec.iov_len = (size_t) 1024;
	
	iov.iov_base = (void *) &transmit_val;
	iov.iov_len = 4;

	if( serv_sock == NULL) {
		printk("Uninitialized socket\n");
		return ;
	}
	//to define LEN
	LEN = 4;
	msg.msg_name = 0;	
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;


//	printk("client len: %ld\n",sizeof(client_test));

	left = LEN;
	//ping pong procedure.

	while( pingcount < 3) {
		//sending message.
		written = 0;	
//repeat_send:
		//IMPORTANT : here is what will be actually transmitted.	
		iov.iov_base = &transmit_val;
		iov_iter_init( &msg.msg_iter, WRITE, &iov, 1, LEN);
//		error =serv_sock->ops->sendmsg(serv_sock, (struct msghdr*)&msg,LEN);
		//try to send the packet via tcp_sendmsg()  function.
	
		error = tcp_sendmsg(serv_sock->sk, (struct msghdr *) &msg, LEN);
		
		if(error < 0) {
			printk("Error in send: %d\n", error);
		}	

		if( (error == -ERESTARTSYS) || (!(flags & MSG_DONTWAIT) && (error == -EAGAIN)) ) {
			printk(" REPETITION \n");	
//			goto repeat_send;
		}

		if(error > 0) {
			written += error;
			left -= error;
		
			if(left){

//				goto repeat_send;
			}
		}
//		printk("CLIENT succesfull TX\n");
		printk("Client send: %d\n", transmit_val);

read_again:

		if(!skb_queue_empty(&serv_sock->sk->sk_receive_queue))
			pr_info("[client] recv queue empty?  %s \n", skb_queue_empty(&serv_sock->sk->sk_receive_queue) ? "yes" : "no");
	
		// IMPORTANT:: in iov.iov_base is where the data will be actually received.
		iov.iov_base = &Buffer;
		iov_iter_init( &rmsg.msg_iter, READ, &iov, 1, 4);

		nonblock = 0; // blocking.
		addr_len = 0;	
		len = tcp_recvmsg(serv_sock->sk, &rmsg, 4, nonblock, kflag, addr_len);
	//	len = kernel_recvmsg(serv_sock, &rmsg, &rvec, 1024, 1024, kflag);


		if(len == -EAGAIN || len == -ERESTARTSYS)
			goto read_again;

//		printk("CLIENT succesfull RX\n");
		printk(KERN_INFO "CLIENT received(int):  %d\n", *Buffer);
		transmit_val = (*Buffer) + 1;
		pingcount++;
	}


	printk("EXIT VALUE of packet in CLIENT: %d\n", transmit_val);


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
	unsigned char in_buf[1024];		

	struct msghdr msg;
	struct kvec vec;
	int len;
	int kflag;

	len = 0;
	memset(in_buf, 0, 1024);

	kflag = 0;	

	if(sock == NULL)
		return -1;
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_flags = 0;

	vec.iov_base = (char *)Buffer;
	vec.iov_len = (size_t) Length;
//	printk(KERN_INFO "Before receiving a message\n");

read_again:

	if(!skb_queue_empty(&sock->sk->sk_receive_queue))
		pr_info("recv queue empty?  %s \n", skb_queue_empty(&sock->sk->sk_receive_queue) ? "yes" : "no");
	
	len = kernel_recvmsg(sock, &msg, &vec, Length, Length, kflag);
	
	if(len == -EAGAIN || len == -ERESTARTSYS)
		goto read_again;

//	printk(KERN_INFO "kenrel_recvmsg returned: %d\n",len);
	printk(KERN_INFO "reception: %s \n", Buffer);
	

	//printk("released socket::RCV\n");

	return len;
}

struct socket * set_up_socket(int port_no)
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

	tcp_init_sock(sock->sk);
	printk("After tcp_init_sock()\n");

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	sin.sin_port = htons(port_no);

	//connecting..

	error = sock->ops->connect(sock, (struct sockaddr*)&sin, sizeof(sin), 0);
	
	if(error) {

		printk(KERN_ALERT "Error connecting socket: %d\n", error);
		return NULL;
	}
	printk(KERN_INFO "connection success.\n");

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

static ssize_t mwrite(struct file *file, const char __user *ubuf, size_t count, loff_t *offset)
{
	int rv;
	char __user *p = (char *) ubuf;

	printk("Client write handler\n");
	printk("size: %d offset: %d\n", (int) count, (int) *offset);

	if(count > BUF_SIZE) 
		return -EFAULT;

	rv = copy_from_user(pin, p, count);

	printk("Module received ::[.write]: %s\n", pin);

	/*	Check for command to start the server thread. */

	if( (my_strcmp(pin, run_command) == -1) && (serv_sock_alive == 0)) {

		printk("Starting the ping-pong [client-side].\n");	

		// creating/connecting a socket in client module.
		serv_sock = set_up_socket(DEFAULT_PORT);
			
		if(serv_sock == NULL) {
			printk( KERN_ALERT "Abnormal termination\n");
			return -1;
		}	
		//reuse socket.
		serv_sock->sk->sk_reuse = 1;
		serv_sock_alive = 1;	//flag for cleanup.
		ping_pong_proc();	
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
	// free-ing the base allocated memory.	
	if(serv_sock_alive) {

		sock_release(serv_sock);
		kfree(base);
		printk("released serv_sock: Module exit\n");
	}
	
	proc_remove(ent);
	printk(KERN_INFO " -- Client Module removed\n");
}

static int module_init0(void)
{
	//client module and its own device in the /proc filesystem.
	ent = proc_create("client_dev", 0660, NULL, &fops);

	printk(KERN_INFO "--Client Module loaded into kernel.\n");

	return (0);
}
module_init(module_init0);
module_exit(module_exit0);
