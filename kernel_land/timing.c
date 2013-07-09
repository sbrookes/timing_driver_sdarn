/* 

   This is the main file for the driver that 
   will control the ADLINK Timing card for the
   super-darn radar system, running on a linux
   machine

   Author: Scott Brookes
   Date: Created 2.1.13

   Note: Driver developed with close reference to 
   "Linux Device Drivers" Third Edition by
   Corbet, Allessandro, Rubini, and Kroah Hartman

   ** DESIGN


   ********

 */

/* */
/* *** */
/* ******* */
/* ************** */
/* **************************** */
#define DEBUG 0 /* debug option */
/* non-zero to debug else zero  */
/* **************************** */
/* ************** */
/* ******* */
/* *** */
/* */

/*
   ATTN: note that some of these may
   be unneccesary... check later
*/
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/errno.h>        /* error codes */
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/fcntl.h>
#include <linux/ioport.h>       /* mapping io */
#include <linux/kdev_t.h>       /* kernel device type */
#include <linux/cdev.h>         /* char device type */
#include <linux/interrupt.h>    /* interrupts */
#include <linux/dma-mapping.h>
#include <asm/irq_vectors.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <asm/uaccess.h>        /* user access */
#include <linux/semaphore.h>    /* synchronization */
#include "timing_kernel_defs.h" /* driver specific header */
#include "_regs_PLX9080.h"

#define MODULE_NAME "timing"

MODULE_LICENSE("Dual BSD/GPL"); /* ??? */
MODULE_AUTHOR("Scott Brookes");

/* register init and exit routines */
module_init(timing_dev_init);
module_exit(timing_dev_exit);

/* global data space */
timing_dev_data  timing_card[TIMING_DEV_COUNT];
timing_dev_data  *master_chip;
int              timing_maj_num;
u8               irq_line;

/* DELETE THIS */
int test_int_alert;
void *dma_virt_addr;
dma_addr_t dma_bus_addr;

/* DMA buffer addresses */
/*
void*      dma_buffers[DMA_BUF_COUNT];
dma_addr_t dma_handles[DMA_BUF_COUNT];
*/

/* pci struct to register with kernel */
/*     so kernel can pair with device */
static struct pci_device_id timing_id[] = {
  { PCI_DEVICE(TIMING_VENDOR_ID, TIMING_DEVICE_ID) },
  {   /* zero field needed by definition */  }
}; /* and the actual registration routine */
MODULE_DEVICE_TABLE(pci, timing_id);

/* for full PCI driver registration with Kernel */
static struct pci_driver timing_driver = {
  .name     = MODULE_NAME,
  .id_table = timing_id,
  .probe    = timing_dev_probe,
  .remove   = timing_dev_remove
};

/* register as char device to read/write */
struct file_operations timing_fops = {
  .owner            = THIS_MODULE,
  .read             = timing_read,
  .write            = timing_write,
  .unlocked_ioctl   = timing_ioctl,
  .open             = timing_dev_open,
  .release          = timing_dev_release
};

/*                 *****                 */
/*             *************             */
/*         *********************         */
/*     *****************************     */
/* ************************************* */
/* ****** KERNEL MODULE FUNCTIONS ****** */
/* ************************************* */
/*     *****************************     */
/*         *********************         */
/*             *************             */
/*                 *****                 */

/* function called at module load time */
static int __init timing_dev_init(void) {

  int i, rc;
  dev_t dev_num;

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_init entry\n");
 #endif

  /* dynamically assign device number */
  rc = alloc_chrdev_region(&dev_num, FIRST_MINOR,
			   TIMING_DEV_COUNT, MODULE_NAME);
  if (rc) {
    printk(KERN_ALERT "Error allocating dev numbers - timing.c\n");
    return rc;
  }
  
  /* record major number */
  timing_maj_num = MAJOR(dev_num);

  /* set up individual data for each char dev */
  /*     for the 8 IO Ports                   */
  for ( i = 0; i < TIMING_DEV_COUNT; i++ ) {
    
    timing_card[i].offset = 0x00;

    /* device number */
    timing_card[i].num = MKDEV(timing_maj_num, FIRST_MINOR + i);

    /* part of device that this addresses */
    if ( i < TIMING_IOPORT_COUNT )
      timing_card[i].component = PCI7300_ID;
    else if ( i < TIMING_IOPORT_COUNT + TIMING_8254_COUNT )
      timing_card[i].component = TIMER8254_ID;
    else 
      timing_card[i].component = PLX9080_ID;
    
    /* vital driver structures */
    timing_card[i].driver = &timing_driver;
    timing_card[i].fops   = &timing_fops  ;

    /* set up actual cdev */
    cdev_init(&timing_card[i].cdev, &timing_fops);
    timing_card[i].cdev.owner = THIS_MODULE;
    timing_card[i].cdev.ops = &timing_fops;

    /* actual cdev registration */ /* magic # 1 is "count" */
    rc = cdev_add(&timing_card[i].cdev, timing_card[i].num, 1);

    if ( rc < 0 ) {
      printk(KERN_ALERT "Error adding timing cdev %d to sys\n", i);
      goto del_cdev;
    }
  } /* end data initialization for IO port loop */
  
 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_init() exit success\n");
 #endif

  /* finally, register as pci dev */
  /* END FLOW OF NORMAL OPERATION */
  return pci_register_driver(&timing_driver);

  /* ERROR HANDLING */
 del_cdev:

  /* unregister char drivers */
  unregister_chrdev_region(dev_num, TIMING_DEV_COUNT);

  /* delete the cdevs that succeeded (up to i) */
  for ( ; !(i < 0); i-- )
    cdev_del(&timing_card[i].cdev);

  return rc;
} /* end timing_init */

/* function called at module unload time */
static void __exit timing_dev_exit(void) {

  int i;

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_exit() entry\n");
 #endif

  /* unregister char drivers */
  unregister_chrdev_region(timing_card[0].num, TIMING_DEV_COUNT);
  
  /* remove char drivers from system */
  for ( i = 0; i < TIMING_DEV_COUNT; i++ )
    cdev_del(&timing_card[i].cdev);

  /* unregister PCI driver */
  pci_unregister_driver(&timing_driver);

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_exit exit normal\n");
 #endif

  return;
} /* end timing_exit */

/*                 *****                 */
/*             *************             */
/*         *********************         */
/*     *****************************     */
/* ************************************* */
/* ******** PCI DEVICE FUNCTION ******** */
/* ************************************* */
/*     *****************************     */
/*         *********************         */
/*             *************             */
/*                 *****                 */


/* TEST FUNCTION FOR INTERRUPT HANDLING */
irqreturn_t timing_interrupt_handler(int irq, void *dev_id) {
  /*
  u8 test8;
  u32 test;

  test = ioread32(timing_card[3].base);

  if ( test & (1 << 2) ) {
    iowrite32( 0x0d, timing_card[3].base);
    test_int_alert = 333;
  }

  test8 = ioread8(timing_card[12].base+0xa9);

  if ( test8 & 0x10 && test8 & 0x1) {
    
    //iowrite8( 0x0c, timing_card[12].base + 0xa9);
    //test_int_alert += 1;

  }
  */
  return IRQ_HANDLED;
}

/* called when kernel matches PCI hardware to this module */
static int timing_dev_probe(struct pci_dev *dev, 
			    const struct pci_device_id *id) {

  int rc, i, j;

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_probe() entry\n");
 #endif

  /* enable the card */
  rc = pci_enable_device(dev);
  if ( rc ) {
    printk(KERN_ALERT "Failed to enable timing card (%d)\n", rc);
    return rc;
  }

  /* first, check if this card is ours... although */
  /*      the vendor ID should have taken care of  */
  /*      this, that ID was actually the bridge    */
  /*      chip We tell this from other cards using */
  /*      the sub_vendorID and sub_deviceID        */
  if ( (ADLINK_VENDOR_ID != dev->subsystem_vendor) ||
       (ADLINK_7300A_ID  != dev->subsystem_device) ) {
    printk(KERN_ALERT "Timing Driver rejected device:    ");
    printk(KERN_ALERT "--sub_vendor ID = %x  ", dev->subsystem_vendor);
    printk(KERN_ALERT "--sub_device ID = %x\n", dev->subsystem_device);

    pci_disable_device(dev);

    return -ENODEV; /* no such device error */
  }

  /* NOW WE ARE DEALING WITH THE TIMING CARD */

  /* enable DMA */
  pci_set_master(dev);

  if ( pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(32)) ) {
    printk(KERN_ALERT "DMA NOT SUPPORTED: Aboting.");
    return -ENODEV; /* not the device we expected */
  }
  else
    printk(KERN_WARNING "Doing DMA with 32 bits\n");

  /* DELETE DMA COHERENT ALLOCATION */
  dma_virt_addr = pci_alloc_consistent(dev, 20*1024, &dma_bus_addr);

  /* retrieve assigned interrupt line number */
  /*      -> see linux/pci.h lines 255 & 256 */
  irq_line = dev->irq;

  /* request interrupt line number */
  /* TODO -- MOVE TO DEVICE OPEN */
  rc = request_irq(irq_line, timing_interrupt_handler,
                   IRQF_SHARED, "timing", timing_card);
  if ( rc ) {
    printk(KERN_ALERT "Failed to register irq %d\n", irq_line);
    return rc;
  }

  /* must claim proprietary access to memory region */
  /*      mapped to the device                      */
  rc = pci_request_regions(dev, timing_driver.name);
  if ( rc ) {
    printk(KERN_ALERT "Memory region collision for TIMING card\n");
    goto request_fail;
  }
  
  /* retrieve base address of mmapped regions */
  timing_card[0].len = pci_resource_len(dev, TIMING_BAR);
  timing_card[0].base = pci_iomap(dev, TIMING_BAR, 
          /* +1 for maxlen */  timing_card[0].len+1);
  
  if (!timing_card[0].base) {
    printk(KERN_ALERT "Failed to find Timing base address\n");
    rc = -ENODEV; /* no device error */
    goto no_base;
  }

  /* already did this for timing_card[0] */
  i = 1;

  /* init other IO port vals */
  for ( j = 1; j < TIMING_IOPORT_COUNT; j++, i++ ) {
    timing_card[i].len  = timing_card[0].len;
    timing_card[i].base = timing_card[0].base + (i*TIMING_IOPORT_SIZE);
  }

  /* and timing vals */
  for ( j = 0; j < TIMING_8254_COUNT; j++, i++ ) {
    timing_card[i].len  = timing_card[0].len;
    timing_card[i].base = timing_card[0].base + (i*TIMING_IOPORT_SIZE);
  }

  /* finally, set up for Bus Mater (LCR) (PLX9080) */
  timing_card[i].len = pci_resource_len(dev, PLX9080_BAR);
  timing_card[i].base = pci_iomap(dev, PLX9080_BAR, 
        /* +1 for maxlen */  timing_card[i].len+1);
  master_chip = &timing_card[i];

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_probe() exit success\n");
 #endif

  /* END NORMAL CONTROL FLOW */
  return 0;

  /* ERROR HANDLING */
 no_base:
  pci_release_regions(dev);
  
 request_fail:
  pci_disable_device(dev);

  pci_clear_master(dev);

  return rc;
} /* end timing_dev_probe */

/* called when PCI device is removed */
static void timing_dev_remove(struct pci_dev *dev) {

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_remove() entry\n");
 #endif

  pci_free_consistent(dev, 20*1024, dma_virt_addr, dma_bus_addr);

  /* release resources */
  free_irq(irq_line, timing_card);
  pci_iounmap(dev, timing_card[0].base);
  pci_iounmap(dev, master_chip->base);
  pci_release_regions(dev);
  pci_clear_master(dev);
  pci_disable_device(dev);

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_dev_remove() exit success\n");
 #endif

  return;
} /* end timing_dev_remove */

/*                 *****                 */
/*             *************             */
/*         *********************         */
/*     *****************************     */
/* ************************************* */
/* ********* CHAR DEV FILE OPS ********* */
/* ************************************* */
/*     *****************************     */
/*         *********************         */
/*             *************             */
/*                 *****                 */

/* called when the char device is opened */
static int timing_dev_open(struct inode *inode, 
			   struct file  *filp ) {

  timing_dev_data *dev;

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_open() entry\n");  
 #endif

  /* need to attach the specific device to filp for */
  /*     other methods to access private data       */
  dev = container_of(inode->i_cdev, timing_dev_data, cdev);
  filp->private_data = dev;

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_open() exit success\n");
 #endif

  return 0;
} /* end timing_dev_open */

/* called when the char device is released */
static int timing_dev_release(struct inode *inode,
			      struct file  *filp ) {

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_release() entry\n");  
 #endif

  /* nothing important to do here */

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_release() exit success\n");
 #endif

  return 0;
} /* end timing_dev_release */

/* Called when the device is read from */
static ssize_t timing_read(struct file *filp, char __user *buf,
			   size_t count, loff_t *f_pos) {

  int rc;
  timing_dev_data *dev;
  
 #if DEBUG != 0
  printk(KERN_DEBUG "timing_read() entry\n");
 #endif

  /* which device is being accessed? */
  dev = filp->private_data;

  rc = copy_to_user(buf, dev->base, count);
  if (rc) {
    printk(KERN_ALERT "timing_read() bad copy_to_user\n");
    return -EFAULT;
  }

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_read() exit success\n");
 #endif

  return 0;
} /* end of timing_read */

static ssize_t dma_debug(struct file *filp, const char __user *buf,
          size_t count, loff_t *f_pos) {

  int rc;

 #if DEBUG != 0
  printk(KERN_DEBUG "dma_debug() entry\n");
 #endif

  /* get data */
  rc = copy_from_user(dma_virt_addr, buf, count);
  if (rc) {
    printk(KERN_ALERT "timing_write() bad copy_from_user\n");
    return -EFAULT;
  }

  /* clear interrupts and disable DMA */
  iowrite8(0x08, timing_card[12].base + 0xa9);
  iowrite8(0x00, timing_card[12].base + 0xa9);

  /* Mode - 32 bit bus, don't increment local addr, enable interrupt */
  iowrite32(cpu_to_le32(0x00000801),   timing_card[12].base + 0x94); 

  /* PCI and local bus addresses, transfer count, transfer direction */
  iowrite32(cpu_to_le32(dma_bus_addr), timing_card[12].base + 0x98);
  iowrite32(cpu_to_le32(0x14),         timing_card[12].base + 0x9c);
  iowrite32(cpu_to_le32(count),        timing_card[12].base + 0xa0);
  iowrite32(0x00,                      timing_card[12].base + 0xa4);

  /* Enable DMA */
  iowrite8( 0x01, timing_card[12].base + 0xa9);
 
  /* Start DMA */
  iowrite8( 0x03, timing_card[12].base + 0xa9);

 #if DEBUG != 0
  printk(KERN_DEBUG "dma_debug() exit success\n");
 #endif 

  return count;
}

/* 
   Called when the device is written to --

   Note: There are 3 different components we could write to.
         This function deals only with writing to the 7300
	 registers. Writing to the 8254 timer or the 9080 chip
	 is dealt with in seperate functions.
*/
static ssize_t timing_write(struct file *filp, const char __user *buf,
			    size_t count, loff_t *f_pos) {

  int rc, curr_count, offset, remaining;
  timing_dev_data *dev;
  uint32_t bounce_buff;

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_write() entry\n");
 #endif

  dev = filp->private_data;

  /* timing chip only takes 1 byte... */
  if ( dev->component == TIMER8254_ID) 
    return chip_8254_write(filp, buf, count, f_pos);

  /* writing to FIFO we want DMA */
  if ( dev == &timing_card[5] )
    return dma_debug(filp, buf, count, f_pos);

  /* going to write MAX 32 bytes at a time */
  remaining = count;
  offset = 0;

  /* while we still need to write */
  while ( remaining > 0 ) {
    
    bounce_buff = 0x0;

    /* if we're going to need to write more than 1 */
    if ( remaining > 4 ) {
      curr_count = 4;
      remaining -= 4;
    }
    else {
      curr_count = remaining;
      remaining = 0;
    }
    
    /* get data */
    rc = copy_from_user(&bounce_buff, buf+offset, curr_count);
    if (rc) {
      printk(KERN_ALERT "timing_write() bad copy_from_user\n");
      return -EFAULT;
    }

    offset += curr_count;

    bounce_buff = cpu_to_le32(bounce_buff);

   #if DEBUG != 0
    printk(KERN_DEBUG "timing_write recvd %x\n", bounce_buff);
   #endif

    /* write data */
    iowrite32(bounce_buff, dev->base);
  }

 #if DEBUG != 0
  printk(KERN_DEBUG "timing_write() exit success\n");
 #endif 

  return count - remaining;
} /* end timing_write */

/* 
   Writing to the 7300 registers is done in 32 bit words while 
       writing to the 8254 chip is done in 8 bits. 
 */
static ssize_t chip_8254_write(struct file *filp, const char __user *buf,
			       size_t count, loff_t *f_pos) {
  int rc;
  timing_dev_data *dev;
  unsigned char msg;

 #if DEBUG != 0
  printk(KERN_DEBUG "8254_write() entry\n");
 #endif

  /* retrieve device data */
  dev = filp->private_data;

  /* 8254 commands and data are just 1 byte */
  if ( count != 1 ) {
    printk(KERN_WARNING "8254 registers are just 8 bits wide\n");
    return -EINVAL;
  }

  /* do actual data retrieval */
  rc = copy_from_user(&msg, buf, count);
  if ( rc < 0 ) {
    printk(KERN_ALERT "8254_write bad copy from user\n");
    return -EFAULT;
  }

  /* preform IO */
  iowrite8(msg, dev->base);

 #if DEBUG != 0
  printk(KERN_DEBUG "8254_write() exit success\n");
 #endif

  return count;
} /* end 8284_chip_write function */

long timing_ioctl(struct file *filp, unsigned int cmd, unsigned long arg) {

  timing_dev_data *dev;

  /* retrieve device info */
  dev = filp->private_data;

  switch(cmd) {

  case CHANGE_PLX_OFFSET:
    
    /* make sure this is the correct device */
    if ( dev->component != PLX9080_ID ) {
      printk(KERN_ALERT "TIMING_IOCTL CHANGE_PLX_OFFSET device NOT PLX\n");
      return -ENOTTY;
    }

    /* offset must be positive and less than 0x100 (see plx 9080 datasheet */
    if ( arg < 0 || arg > 0x100 ) { 
      printk(KERN_ALERT "TIMING_IOCTL CHANGE_PLX_OFFSET bad argument\n");
      return -ENOTTY;
    }
    
    dev->offset = arg;

   #if DEBUG != 0
    printk(KERN_DEBUG "new plx9080 offset is -- %x", dev->offset);
   #endif
    
    return 0; /* success */
  /* END CASE CHANGE_PLX_OFFSET */

  default:
    printk(KERN_DEBUG "TIMING_IOCTL bad command\n");
    return -ENOTTY;

  } /* end command switch statement */

  /* never reached */
  return -EINVAL;
} /* end of ioctl command */
