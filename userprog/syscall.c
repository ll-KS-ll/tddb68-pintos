#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "devices/input.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

/* Reads a byte at user virtual address UADDR.
   UADDR must be below PHYS_BASE.
   Returns the byte value if successful, -1 if a segfault
   occurred. */
static int
get_user (const uint8_t *uaddr)
{
  int result;
  asm ("movl $1f, %0; movzbl %1, %0; 1:"
     : "=&a" (result) : "m" (*uaddr));
  return result;
}
 
/* Writes BYTE to user address UDST.
   UDST must be below PHYS_BASE.
   Returns true if successful, false if a segfault occurred. */
static bool
put_user (uint8_t *udst, uint8_t byte)
{
  int error_code;
  asm ("movl $1f, %0; movb %b2, %1; 1:"
     : "=&a" (error_code), "=m" (*udst) : "r" (byte));
  return error_code != -1;
}

/* Validate and return argument n. */
static uint32_t
get_argument(void* esp, int argn)
{
  uint32_t* argv = esp + (argn + 1) * 4;
  if(get_user(argv + 3) != -1) /* Test last byte of argument. */
    return *argv;
  thread_current()->exit_status = -1;
  thread_exit();
  NOT_REACHED();
}

/* Execute system call halt. */
static void
halt( void )
{
  power_off();
}

/* Check for null arguments and bad pointers. */
static void
validate_pointer(char *c, unsigned int size) {
  int n;
  for(n = 0; n < size; n++) {
    if((*c+n) == NULL || !is_user_vaddr((*c+n)) || get_user((*c+n)) == -1) {
      thread_current()->exit_status = -1;
      thread_exit();
      break;
    }
  }
}

/* Execute system call create. */
static bool
create( void *esp )
{
  /* Get arguments. */
  const char* name = get_argument(esp, 0);
  unsigned int size = get_argument(esp, 1);
  
  /* Check for null arguments and bad pointers. */
  validate_pointer(name, size);

  /* Try to create file. */
  bool status = filesys_create(name, size);

  /* Return status of file creation to user program. */
  return status;
}

/* Execute system call open. */
static int
open( void *esp )
{
  /* Open file in filesystem. */
  const char* name = get_argument(esp, 0);
  /* Check for null arguments and bad pointers. */
  validate_pointer(name, 14); // Filename is maximum of 14 chars.
  
  /* Create file descriptor. */
  struct thread* t = thread_current();
  int fd = bitmap_scan_and_flip(t->fd_bitmap, 0, 1, 0); 
  if (fd == BITMAP_ERROR) /* Couldn't find a free file descriptor. */
    return -1;            /* Can't open file. */
  
  struct file* f = filesys_open(name);
  
  if (f == NULL) {  /* Couldn't open file with given name. */
    bitmap_reset(t->fd_bitmap, fd); /* Reset unused file descriptor. */  
    return -1;
  }

  t->files[fd] = f;
  return fd + 2; /* Fd 0 & 1 are reserved for console. Skip those.*/
}

/* Check if file descriptor is valid. */
static bool 
valid_fd(int fd)
{   
  struct thread *t = thread_current();
  return fd >= 0 && 
          fd < FD_SIZE && 
          bitmap_test(t->fd_bitmap, fd);
}

/* Execute system call write. */
static int
write(void *esp)
{
  /* Get arguments. */
  int fd = get_argument(esp, 0);
  const void* buffer = get_argument(esp, 1);
  unsigned int size = get_argument(esp, 2);

  validate_pointer(buffer, size);

  if(fd == STDOUT_FILENO) {
    int n = size;
    
    while(n >= CONSOLE_BUFFER_SIZE) {
      putbuf(buffer, CONSOLE_BUFFER_SIZE);
      n -= CONSOLE_BUFFER_SIZE;
      buffer += CONSOLE_BUFFER_SIZE;
    }
  
    putbuf(buffer, n);    
  
    return size;
  } else if (fd == STDIN_FILENO) {
    return -1; // Can't write to read from console.
  } else {  // Write to file with fd.
    /* File descriptors 0 & 1 are reserved for console. Skip those.*/
    fd -= 2; 

    /* Control file descriptor. */
    if(!valid_fd(fd))
      return -1;

    struct file* f = thread_current()->files[fd];
    return file_write(f, buffer, size);
  }
}

/* Execute system call read. */
static int
read( void *esp )
{
  /* Get arguments. */
  int fd = get_argument(esp, 0);
  uint8_t* buf = get_argument(esp, 1);
  unsigned int size = get_argument(esp, 2);

  validate_pointer(buf, size);  

  if (fd == STDIN_FILENO) {
    int n = 0;
    
    while(n < size){
      *buf = input_getc(); /* Get a single key from keyboard. */
      buf++; /* Go to next element in buffer. */
      n++;
    }
    
    return n;
  } else if (fd == STDOUT_FILENO) {
    return -1; // Can't read from write to console.
  } else {
    /* File descriptors 0 & 1 are reserved for console. Skip those.*/
    fd -= 2; 
    /* Control file descriptor. */
    if(!valid_fd(fd))
      return -1;
    
    struct file* f = thread_current()->files[fd];
    return file_read(f, buf, size);
  }
}

/* Execute system call seek. */
static void
seek( void *esp )
{
  /* Get arguments. */
  int fd = get_argument(esp, 0);
  unsigned position = get_argument(esp, 1);
  /* File descriptors 0 & 1 are reserved for console. Skip those.*/
  fd -= 2; 
  /* Control file descriptor. */
  if(!valid_fd(fd))
    return -1;

  struct file* f = thread_current()->files[fd];
  file_seek(f, position);
}

/* Execute system call tell. */
static unsigned
tell( void *esp )
{
  /* Get arguments. */
  int fd = get_argument(esp, 0);
  /* File descriptors 0 & 1 are reserved for console. Skip those.*/
  fd -= 2; 
  /* Control file descriptor. */
  if(!valid_fd(fd))
    return -1;

  unsigned pos = -1;
  struct file* f = thread_current()->files[fd];
  pos = file_tell(f);

  return pos;
}

/* Execute system call filesize. */
static int
filesize( void *esp ){
  int fd = get_argument(esp, 0);
  /* File descriptors 0 & 1 are reserved for console. Skip those.*/
  fd -= 2; 
  /* Control file descriptor. */
  if(!valid_fd(fd))
    return -1;

  unsigned size = -1;
  struct file* f = thread_current()->files[fd];
  size = file_length(f);

  return size;
}

/* Execute system call close. */
static bool
remove( void *esp )
{
  /* Get arguments. */
  const char* name = get_argument(esp, 0);

  validate_pointer(name, 14); // Maximum of 14 chars in file name.

  bool success = false;
  success = filesys_remove(name);

  return success;
}

/* Execute system call close. */
static void
close( void *esp )
{
  /* Get arguments. */
  int fd = get_argument(esp, 0) - 2;

  /* Validate file descriptor. */
  if(!valid_fd(fd)) return;

  struct thread *t = thread_current();
  struct file *f = t->files[fd];

  /* Close file. */
  file_close(f); 
  /* Clear file descriptor. */
  bitmap_reset(t->fd_bitmap, fd);
}

/* Execute system call execute. */
static int
exec( void* esp )
{
  const char* cmd_line = get_argument(esp, 0);
  validate_pointer(cmd_line, 1); 
  int pid = process_execute(cmd_line);
  return pid;
}

static int
wait( void* esp)
{
  int pid = get_argument(esp, 0);
  int exit_status = process_wait(pid);
  return exit_status;
}


/* Execute system call exit. */
static void
exit( void* esp )
{
  int status = get_argument(esp, 0);
  /* Set exit code. */
  thread_current()->exit_status = status;
  /* Exit process. */
  thread_exit();
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* Get syscall number from stack. */
  int* esp = f->esp;
  
  /* Check so pointers aren't in kernel memory. */
  if(!is_user_vaddr(esp) || !is_user_vaddr(esp + 1) || !is_user_vaddr(esp + 2) || !is_user_vaddr(esp + 3)) {
    /* Exit process. */
    thread_current()->exit_status = -1;
    thread_exit();
  }
  
  if(get_user(esp) == -1 || get_user(esp + 1) == -1 || get_user(esp + 2) == -1 || get_user(esp + 3) == -1){
    /* Exit process. */
    thread_current()->exit_status = -1;
    thread_exit();
  }
    
  if(*esp < SYS_HALT || *esp > SYS_REMOVE) {
    /* Exit process. */
    thread_current()->exit_status = -1;
    thread_exit();
  }
  
  int sys_nr = *esp;
  switch(sys_nr) {
    case SYS_HALT: // Shutdown machine
      halt();
      NOT_REACHED();
      break;

    case SYS_CREATE: // Create a file with specified size.
      f->eax = create(esp);
      break;

    case SYS_OPEN: // If possible open file.
      f->eax = open(esp); 
      break;

    case SYS_WRITE: // Write to file or consol.
      f->eax = write(esp);
      break;

    case SYS_READ: // Read from file or console.
      f->eax = read(esp);
      break;

    case SYS_SEEK:  // Seek to a position in a file.
      seek(esp);
      break;

    case SYS_TELL:  // Tell a position in a file.
      f->eax = tell(esp);
      break;

    case SYS_FILESIZE:  // Get size of a file.
      f->eax = filesize(esp);
      break;

    case SYS_REMOVE:  // Remove a file from file system.
      f->eax = remove(esp);
      break;

    case SYS_CLOSE: // Close file.
      close(esp);
      break;

    case SYS_EXEC: // Start a process
      f->eax = exec(esp);
      break;

    case SYS_WAIT:
      f->eax = wait(esp);
      break;

    case SYS_EXIT: // Exit process.
      exit(esp);
      break;

    default: 
      printf ("Unknown system call %d\n", sys_nr);
      thread_exit ();
  }

}
