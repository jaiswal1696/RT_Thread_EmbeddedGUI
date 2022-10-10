#include <rtthread.h>

#define LOG_TAG         "mnt"
#define DBG_ENABLE
#define DBG_SECTION_NAME "mnt"
#define DBG_LEVEL DBG_ERROR
#define DBG_COLOR
#include <rtdbg.h>

#include <dfs_fs.h>
#include <dfs_file.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/statfs.h>

#if defined(RT_USING_FAL)
    #include <fal.h>
#endif

#if defined(PKG_USING_RAMDISK)
    #define RAMDISK_NAME         "ramdisk0"
    #define RAMDISK_UDC          "ramdisk1"
    #define MOUNT_POINT_RAMDISK0 "/"
#endif

#if defined(BOARD_USING_STORAGE_SPIFLASH)
    #define PARTITION_NAME_FILESYSTEM "filesystem"
    #define MOUNT_POINT_SPIFLASH0 "/mnt/"PARTITION_NAME_FILESYSTEM
#endif

#ifdef RT_USING_DFS_MNTTABLE

/*
const char   *device_name;
const char   *path;
const char   *filesystemtype;
unsigned long rwflag;
const void   *data;
*/

const struct dfs_mount_tbl mount_table[] =
{
#if defined(PKG_USING_RAMDISK)
    { RAMDISK_UDC, "/mnt/ram_usbd", "elm", 0, RT_NULL },
#endif

#if defined(BOARD_USING_STORAGE_SPIFLASH)
    { PARTITION_NAME_FILESYSTEM, "/mnt/filesystem", "elm", 0, RT_NULL },
#endif
#if defined(PKG_USING_DFS_YAFFS)
    { "nand1", "/mnt/fminand", "yaffs", 0, RT_NULL },
#endif

    {0},
};
#endif


#if defined(PKG_USING_RAMDISK)

extern rt_err_t ramdisk_init(const char *dev_name, rt_uint8_t *disk_addr, rt_size_t block_size, rt_size_t num_block);
int ramdisk_device_init(void)
{
    rt_err_t result = RT_EOK;

    /* Create a 8MB RAMDISK */
    result = ramdisk_init(RAMDISK_NAME, NULL, 512, 2 * 4096);
    RT_ASSERT(result == RT_EOK);

    /* Create a 4MB RAMDISK */
    result = ramdisk_init(RAMDISK_UDC, NULL, 512, 2 * 4096);
    RT_ASSERT(result == RT_EOK);

    return 0;
}
INIT_DEVICE_EXPORT(ramdisk_device_init);

/* Recursive mkdir */
static int mkdir_p(const char *dir, const mode_t mode)
{
    int ret = -1;
    char *tmp = NULL;
    char *p = NULL;
    struct stat sb;
    rt_size_t len;

    if (!dir)
        goto exit_mkdir_p;

    /* Copy path */
    /* Get the string length */
    len = strlen(dir);
    tmp = rt_strdup(dir);

    /* Remove trailing slash */
    if (tmp[len - 1] == '/')
    {
        tmp[len - 1] = '\0';
        len--;
    }

    /* check if path exists and is a directory */
    if (stat(tmp, &sb) == 0)
    {
        if (S_ISDIR(sb.st_mode))
        {
            ret = 0;
            goto exit_mkdir_p;
        }
    }

    /* Recursive mkdir */
    for (p = tmp + 1; p - tmp <= len; p++)
    {
        if ((*p == '/') || (p - tmp == len))
        {
            *p = 0;

            /* Test path */
            if (stat(tmp, &sb) != 0)
            {
                /* Path does not exist - create directory */
                if (mkdir(tmp, mode) < 0)
                {
                    goto exit_mkdir_p;
                }
            }
            else if (!S_ISDIR(sb.st_mode))
            {
                /* Not a directory */
                goto exit_mkdir_p;
            }
            if (p - tmp != len)
                *p = '/';
        }
    }

    ret = 0;

exit_mkdir_p:

    if (tmp)
        rt_free(tmp);

    return ret;
}

#if defined(PKG_USING_DFS_YAFFS) && defined(RT_USING_DFS_MNTTABLE)
#include "yaffs_guts.h"
void yaffs_dev_init(void)
{
    int i;
    for (i = 0; i < sizeof(mount_table) / sizeof(struct dfs_mount_tbl); i++)
    {
        if (mount_table[i].filesystemtype && !rt_strcmp(mount_table[i].filesystemtype, "yaffs"))
        {
            struct rt_mtd_nand_device *psMtdNandDev = RT_MTD_NAND_DEVICE(rt_device_find(mount_table[i].device_name));
            if (psMtdNandDev)
            {
                yaffs_start_up(psMtdNandDev, (const char *)mount_table[i].path);
            }
        }
    }
}
#endif

/* Initialize the filesystem */
int filesystem_init(void)
{
    rt_err_t result = RT_EOK;

#if defined(RT_USING_FAL)
    extern int fal_init_check(void);
    if (!fal_init_check())
        fal_init();
#endif

    // ramdisk as root
    if (!rt_device_find(RAMDISK_NAME))
    {
        LOG_E("cannot find %s device", RAMDISK_NAME);
        result = -RT_ERROR;
        goto exit_filesystem_init;
    }
    else
    {
        /* Format these ramdisk */
        result = (rt_err_t)dfs_mkfs("elm", RAMDISK_NAME);
        RT_ASSERT(result == RT_EOK);

        /* mount ramdisk0 as root directory */
        if (dfs_mount(RAMDISK_NAME, "/", "elm", 0, RT_NULL) == 0)
        {
            LOG_I("ramdisk mounted on \"/\".");

            /* now you can create dir dynamically. */
            mkdir_p("/mnt", 0x777);
            mkdir_p("/cache", 0x777);
            mkdir_p("/download", 0x777);
            mkdir_p("/mnt/ram_usbd", 0x777);
            mkdir_p("/mnt/filesystem", 0x777);
            mkdir_p("/mnt/fminand", 0x777);
#if defined(RT_USBH_MSTORAGE) && defined(UDISK_MOUNTPOINT)
            mkdir_p(UDISK_MOUNTPOINT, 0x777);
#endif
        }
        else
        {
            LOG_E("root folder creation failed!\n");
            goto exit_filesystem_init;
        }
    }

    if (!rt_device_find(RAMDISK_UDC))
    {
        LOG_E("cannot find %s device", RAMDISK_UDC);
        goto exit_filesystem_init;
    }
    else
    {
        /* Format these ramdisk */
        result = (rt_err_t)dfs_mkfs("elm", RAMDISK_UDC);
        RT_ASSERT(result == RT_EOK);
    }

#if defined(BOARD_USING_STORAGE_SPIFLASH)
    {
        struct rt_device *psNorFlash = fal_blk_device_create(PARTITION_NAME_FILESYSTEM);
        if (!psNorFlash)
        {
            rt_kprintf("Failed to create block device for %s.\n", PARTITION_NAME_FILESYSTEM);
        }
    }
    
 const char *szKeyLabel[] =
{
    "K1",
    "K2",
    "K3",
    "K4",
    "K5",
    "K6"
};

static void nu_key_matrix_cb(void *args)
{
    uint32_t ri = (uint32_t)args;
    int ci;
    for (ci = 0; ci < MATRIX_COL_NUM; ci++)
    {
        /* Find column bit is low. */
        if (!rt_pin_read(au32KeyMatrix_Col[ci]))
        {
            break;
        }
    }
    rt_kprintf("[%d %d] Pressed %s button.\n", ci, ri, szKeyLabel[(ci) + MATRIX_COL_NUM * ri]);
}

static void nu_key_matrix_switch(uint32_t counter)
{
    int i;
    for (i = 0; i < MATRIX_COL_NUM; i++)
    {
        /* set pin value to high */
        rt_pin_write(au32KeyMatrix_Col[i], PIN_HIGH);
    }
    /* set pin value to low */
    rt_pin_write(au32KeyMatrix_Col[counter % MATRIX_COL_NUM], PIN_LOW);
}
#endif

int main(int argc, char **argv)
{
#if defined(RT_USING_PIN)
    uint32_t counter = 1;
    int i = 0;

    for (i = 0; i < MATRIX_ROW_NUM; i++)
    {
        /* set pin mode to input */
        rt_pin_mode(au32KeyMatrix_Row[i], PIN_MODE_INPUT_PULLUP);

        rt_pin_attach_irq(au32KeyMatrix_Row[i], PIN_IRQ_MODE_FALLING, nu_key_matrix_cb, (void *)i);
        rt_pin_irq_enable(au32KeyMatrix_Row[i], PIN_IRQ_ENABLE);
    }

    for (i = 0; i < MATRIX_COL_NUM; i++)
    {
        /* set  pin mode to output */
        rt_pin_mode(au32KeyMatrix_Col[i], PIN_MODE_OUTPUT);
    }

    /* set INDICATOR_LED pin mode to output */
    rt_pin_mode(INDICATOR_LED, PIN_MODE_OUTPUT);

    /* Toggle column pins in key matrix. */
    while (counter++ > 0)
    {
        rt_pin_write(INDICATOR_LED, PIN_HIGH);
        rt_thread_mdelay(200);
        rt_pin_write(INDICATOR_LED, PIN_LOW);
        rt_thread_mdelay(200);
        nu_key_matrix_switch(counter);
    }
#endif

    return 0;
}   
    
#endif

#if defined(PKG_USING_DFS_YAFFS) && defined(RT_USING_DFS_MNTTABLE)
    yaffs_dev_init();
#endif

exit_filesystem_init:

    return -result;
}
INIT_ENV_EXPORT(filesystem_init);
#endif
