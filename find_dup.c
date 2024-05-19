#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>
#include <linux/dirent.h>
#include <linux/namei.h>

// 1KB
#define BUF_SIZE 1024

static char *dir_path = "";
static char *filename = "";

module_param(dir_path, charp, 0000); 
MODULE_PARM_DESC(dir_path, "Directory to search for duplicates");

module_param(filename, charp, 0000);
MODULE_PARM_DESC(filename, "Filename to compare for duplicates");

struct file *filep;

static int compare_files(struct file *file1, struct file *file2) {
    char buf1[BUF_SIZE], buf2[BUF_SIZE];
    ssize_t bytes_read1, bytes_read2;
    file1->f_pos = 0;
    file2->f_pos = 0;

	do{
		bytes_read1 = kernel_read(file1, buf1, sizeof(buf1), &file1->f_pos);
		bytes_read2 = kernel_read(file2, buf2, sizeof(buf2), &file2->f_pos);

		if (bytes_read1 < 0 || bytes_read2 < 0) {
            // Handle read errors
            return -EIO;
        }

		if (bytes_read1 != bytes_read2 || memcmp(buf1, buf2, bytes_read1) != 0) {
		    // not duplicates
		    return 0;
		}
	} while(bytes_read1 > 0 && bytes_read2 > 0);
	
	// duplicates
    return 1;
}

// Callback for 'iterate_dir'
static _Bool find_dup_subdir(struct dir_context *ctx, const char *name, int namelen, loff_t offset, u64 ino, unsigned int d_type)
{
	// not compare automatically generated files, compare only regular files (not directories)
	if(name[0] == '.' || d_type != DT_REG){
    	return 1;
	}

	size_t dir_path_len = strlen(dir_path);
	
	// exclude "." at start of name, add '/', add '\0'
	char* compare_file_name;
	size_t compare_file_name_len = dir_path_len + namelen + 2;
	compare_file_name = kmalloc(compare_file_name_len, GFP_KERNEL);
	
	if (!compare_file_name) {
		printk(KERN_ERR "Duplicate Finder: Error allocating memory for compare_file_name\n");
		kfree(compare_file_name);
		return -ENOMEM;
	}
	
	snprintf(compare_file_name, compare_file_name_len, "%s/%s", dir_path, name);
	
	struct file *compare_file = filp_open(compare_file_name, O_RDONLY, 0);
	if (IS_ERR(compare_file)) {
		printk(KERN_ERR "Duplicate Finder: Error opening compare_file\n");
		kfree(compare_file_name);
		return PTR_ERR(compare_file);
	}
	
	if (compare_files(filep, compare_file)) {
		printk(KERN_INFO "Duplicate Finder: Duplicate found: %s = %s\n", filename, compare_file_name);
	}
	
	filp_close(compare_file, NULL); 
    kfree(compare_file_name);
    return 1;
}

static int __init find_dup_init(void) {
    struct file *dir;
    struct dir_context ctx;

    printk(KERN_INFO "Duplicate Finder: Starting...\n");

    dir = filp_open(dir_path, O_RDONLY, 0);
    if (IS_ERR(dir)) {
        printk(KERN_ERR "Duplicate Finder: Error opening dir_path\n");
        return PTR_ERR(dir);
    }
    filep = filp_open(filename, O_RDONLY, 0);
    if (IS_ERR(filep)) {
    	printk(KERN_ERR "Duplicate Finder: Error opening filep\n");
        return PTR_ERR(filep);
    }
    ctx.actor = &find_dup_subdir;
    ctx.pos = 0;
	
    iterate_dir(dir, &ctx);

    filp_close(filep, NULL);
    filp_close(dir, NULL);
    printk(KERN_INFO "Duplicate Finder: Finished\n");
    return 0;
}

static void __exit find_dup_exit(void) {
    printk(KERN_INFO "Duplicate Finder: Exiting...\n");
}

module_init(find_dup_init);
module_exit(find_dup_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ivan Levin");
MODULE_DESCRIPTION("Duplicate File Finder Kernel Module");

