#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <termios.h>
#include <pthread.h>
#include <sys/utsname.h>
#include "ql-usb.h"

#define LOG_NDEBUG 0
#define LOG_TAG "RILU"
#include "ql-log.h"

#define USBID_LEN 4

static struct utsname utsname;  /* for the kernel version */
static int kernel_version;
#define KVERSION(j,n,p) ((j)*1000000 + (n)*1000 + (p))

struct ql_usb_id_struct {
    unsigned short vid;
    unsigned short pid;
    unsigned short at_inf;
    unsigned short ppp_inf;
};

static const struct ql_usb_id_struct ql_usb_id_table[] = {
    {0x05c6, 0x9003, 2, 3}, //UC20
    {0x05c6, 0x9090, 2, 3}, //UC15
    {0x05c6, 0x9215, 2, 3}, //EC20
    {0x1519, 0x0331, 6, 0}, //UG95
    {0x1519, 0x0020, 6, 0}, //UG95
    {0x05c6, 0x9025, 2, 3}, //EC25
    {0x2c7c, 0x0125, 2, 3}, //EC25
    {0x2c7c, 0x0121, 2, 3}, //EC25
};

static int ql_get_usbnet_adapter(struct ql_usb_device_info *pusb_device_info);

int is_usb_match(unsigned short vid, unsigned short pid) {
    size_t i;
    for (i = 0; i < ARRAY_SIZE(ql_usb_id_table); i++)
    {
        if (vid == ql_usb_id_table[i].vid) 
        {
            if (pid == 0x0000) //donot check pid
                return 1;
            else if (pid == ql_usb_id_table[i].pid)
                return 1;
        }
    }
    return 0;
}
    
static int idusb2hex(char idusbinfo[USBID_LEN]) {
    int i;
    int value = 0;
    for (i = 0; i < USBID_LEN; i++) {
        if (idusbinfo[i] < 'a')
            value |= ((idusbinfo[i] - '0') << ((3 - i)*4));
        else
            value |= ((idusbinfo[i] - 'a' + 10) << ((3 - i)*4));
    }
    return value;
}

int ql_find_usb_device(struct ql_usb_device_info *pusb_device_info)//struct ql_usb_device_info *usb_device_info, char *dir)
{ 
    DIR *pDir;
    int fd;
    char filename[MAX_PATH];
    int find_usb_device = 0;
    struct stat statbuf;
    struct dirent* ent = NULL;
    int idVendor,idProduct;
    struct ql_usb_device_info usb_device_info;
    char dir[MAX_PATH] = {0};
    
    strcat(dir, "/sys/bus/usb/devices");
    if ((pDir = opendir(dir)) == NULL)  {
        LOGE("Cannot open directory:%s/", dir);  
        return -1;
    }
    while ((ent = readdir(pDir)) != NULL)  {
        memset(&usb_device_info, 0, sizeof(usb_device_info));
        sprintf(filename, "%s/%s", dir, ent->d_name);
        lstat(filename, &statbuf);
        if (S_ISLNK(statbuf.st_mode))  {
            char idusbinfo[USBID_LEN+1] = {0};
            idVendor = idProduct = 0x0000;
            sprintf(filename, "%s/%s/idVendor", dir, ent->d_name);
            fd = open(filename, O_RDONLY);
            if (fd > 0) {
                if (4 == read(fd, idusbinfo, USBID_LEN))
                    idVendor = idusb2hex(idusbinfo);
                close(fd);
            }
            if (!is_usb_match(idVendor, idProduct))
                continue;
            sprintf(filename, "%s/%s/idProduct", dir, ent->d_name);
            fd = open(filename, O_RDONLY);
            if (fd > 0) {
                if (4 == read(fd, idusbinfo, USBID_LEN))
                    idProduct = idusb2hex(idusbinfo);
                close(fd);
            }
            if (!is_usb_match(idVendor, idProduct))
                continue;

            if (find_usb_device > MAX_CARD_NUM){
                LOGE("usb device number is more than max number,please repair it");
                break;
            }
            
            strcpy(usb_device_info.usb_device_name, ent->d_name);
            usb_device_info.idProduct = idProduct;
            usb_device_info.idVendor = idVendor;
            strcpy(usb_device_info.usbdevice_pah, dir);
            
            memcpy(&pusb_device_info[find_usb_device], &usb_device_info, sizeof(struct ql_usb_device_info));
            find_usb_device++;
        }
    }
    closedir(pDir);
    return find_usb_device;
}

static int ql_get_usb_inteface(int usb_interface, int idVendor, int idProduct)
{
    size_t i;
    if (usb_interface == USB_AT_INF)
        sleep(1); //wait load usb driver
    
    for (i = 0; i < ARRAY_SIZE(ql_usb_id_table); i++) {
        if ((idVendor == ql_usb_id_table[i].vid) && (idProduct == ql_usb_id_table[i].pid)) {
            if (usb_interface == USB_AT_INF) {
                usb_interface = ql_usb_id_table[i].at_inf;
                break;
            } else if (usb_interface == USB_PPP_INF) {
                usb_interface = ql_usb_id_table[i].ppp_inf;
                break;
            }
        }
    }

    if (i == ARRAY_SIZE(ql_usb_id_table))
        return -1;
    
    return usb_interface;
}


char *ql_get_out_ttyname(int usb_interface, struct ql_usb_device_info *usb_device_info)
{
    DIR *pDir;
    struct dirent* ent = NULL;
    char dir[MAX_PATH]={0};
    char out_ttyname[32] = {0};
    
    strcat(dir, usb_device_info->usbdevice_pah);

    int idVendor = usb_device_info->idVendor;
    int idProduct = usb_device_info->idProduct;

    usb_interface = ql_get_usb_inteface(usb_interface, idVendor, idProduct);

    if(usb_interface < 0){
        return NULL;
    }

    if (usb_device_info->usb_device_name[0]) {
        char usb_inf_path[20];
        sprintf(usb_inf_path, ":1.%d", usb_interface);

        strcat(dir, usb_inf_path);
        if ((pDir = opendir(dir)) == NULL) {
            LOGE("Cannot open directory:%s/", dir);              
            return NULL;
        }
    
        while ((ent = readdir(pDir)) != NULL) {
            if (strncmp(ent->d_name, "tty", 3) == 0) {
                LOGD("find vid=0x%04x, pid=0x%04x, tty=%s", idVendor, idProduct, ent->d_name);
                strcpy(out_ttyname, ent->d_name);
                break;
            }
        }
        closedir(pDir); 
    }

    if (strcmp(out_ttyname, "tty") == 0) { //find tty not ttyUSBx or ttyACMx
        strcat(dir, "/tty");
        if ((pDir = opendir(dir)) == NULL)  {  
            LOGE("Cannot open directory:%s/", dir);      
            return NULL;
        }
    
        while ((ent = readdir(pDir)) != NULL)  {
            if (strncmp(ent->d_name, "tty", 3) == 0) {
                LOGD("find vid=0x%04x, pid=0x%04x, tty=%s", idVendor, idProduct, ent->d_name);
                strcpy(out_ttyname, ent->d_name);
                break;
            } 
        }
        closedir(pDir); 
    }
    
    if (out_ttyname[0] == 0 && idVendor != ql_usb_id_table[3].vid) {
        if (access("/sys/bus/usb-serial/drivers/option1/new_id", W_OK) == 0) {
            char *cmd;
            LOGE("find usb serial option driver, but donot cantain quectel vid&pid");
            asprintf(&cmd, "echo 0x%x 0x%x > /sys/bus/usb-serial/drivers/option1/new_id", idVendor, idProduct);
            system(cmd);
            free(cmd);   
        } else {
            LOGE("can not find usb serial option driver");
        }
    }

    if (out_ttyname[0]) {
        strcpy(usb_device_info->ttyAT_name, out_ttyname);
        return usb_device_info->ttyAT_name;
    }

    return NULL;
}

int ql_get_usb_device_info(int usb_interface) 
{
    struct dirent* ent = NULL;  
    DIR *pDir;  
    char dir[MAX_PATH], filename[MAX_PATH];
    int idVendor = 0, idProduct = 0;
    int find_usb_device = 0;

    int i = 0;    
    struct ql_usb_device_info usb_device_info[MAX_CARD_NUM];
    memset(usb_device_info, 0, MAX_CARD_NUM * sizeof(struct ql_usb_device_info));

    find_usb_device = ql_find_usb_device(usb_device_info);
    
    if(!find_usb_device) {
        return 0;
    }    

    for(i = 0; i < find_usb_device; i++) {
        strcat(usb_device_info[i].usbdevice_pah, "/");
        strcat(usb_device_info[i].usbdevice_pah, usb_device_info[i].usb_device_name);

        if(USB_AT_INF == usb_interface || USB_PPP_INF == usb_interface) {
            ql_get_out_ttyname(usb_interface, &usb_device_info[i]);
        }
        
        ql_get_usbnet_adapter(&usb_device_info[i]);
    }

    memcpy(s_usb_device_info, usb_device_info, sizeof(usb_device_info));

    return find_usb_device;
}

char * ql_get_ttyname(int usb_interface, char *out_ttyname)
{
    int cnt = ql_get_usb_device_info(usb_interface);
        
    if( 0 < cnt ) {
        strcpy(out_ttyname, s_usb_device_info[0].ttyAT_name);
        return (0 == s_usb_device_info[0].ttyAT_name[0]) ? NULL : s_usb_device_info[0].ttyAT_name;   
    }

    return NULL;
}

char *ql_get_usb_class_name()
{
    char *usb_class_name = NULL;
    int osmaj, osmin, ospatch;

    /* get the kernel version now, since we are called before sys_init */
    uname(&utsname);
    osmaj = osmin = ospatch = 0;
    sscanf(utsname.release, "%d.%d.%d", &osmaj, &osmin, &ospatch);
    kernel_version = KVERSION(osmaj, osmin, ospatch);
    if (kernel_version < KVERSION(3, 6, 0)) {
        usb_class_name = "usb";
    } else {
        usb_class_name = "usbmisc";
    }

    return usb_class_name;
}


int ql_make_node(char *dir, char *usb_device_name)
{
    char subdir[MAX_PATH]={0};
    DIR *pDir, *pSubDir;
    struct dirent* ent = NULL;
    struct dirent* subent = NULL;
#define CDCWDM_UEVENT_LEN 256
#ifndef MKDEV
#define MKDEV(ma,mi) ((ma)<<8 | (mi))
#endif
    int fd_uevent = -1;
    char uevent_path[MAX_PATH] = {0};
    char cdc_nod[MAX_PATH] = {0};
    char uevent_buf[CDCWDM_UEVENT_LEN] = {0};
    char *pmajor = NULL;
    char *pminor = NULL;
    char *pcr = NULL;
    int cdc_major = 0;
    int cdc_minor = 0;
    struct stat st = {0};
    int need_newnod = 0;
    int find_qmichannel = 0;

    strcpy(subdir, dir);
    strncat(subdir, "/", strlen("/"));
    strncat(subdir, usb_device_name, strlen(usb_device_name));
    if ((pSubDir = opendir(subdir)) == NULL)  {  
        LOGE("Cannot open directory:%s/", subdir);
        return -ENODEV;
    }
    while ((subent = readdir(pSubDir)) != NULL) {
        if (strncmp(subent->d_name, "cdc-wdm", strlen("cdc-wdm")) == 0) {
            LOGD("Find qmichannel = %s", subent->d_name);
            find_qmichannel = 1;
        #if 1
            snprintf(uevent_path, MAX_PATH, "%s/%s/%s", subdir, subent->d_name, "uevent");
            fd_uevent = open(uevent_path, O_RDONLY);
            if (fd_uevent < 0) {
                LOGE("Cannot open file:%s, errno = %d(%s)", uevent_path, errno, strerror(errno));
            } else {
                snprintf(cdc_nod, MAX_PATH, "/dev/%s", subent->d_name);
                read(fd_uevent, uevent_buf, CDCWDM_UEVENT_LEN);
                close(fd_uevent);
                pmajor = strstr(uevent_buf, "MAJOR");
                pminor = strstr(uevent_buf, "MINOR");
                if (pmajor && pminor) {
                    pmajor += sizeof("MAJOR");
                    pminor += sizeof("MINOR");
                    pcr = pmajor;
                    while (0 != strncmp(pcr++, "\n", 1));
                    *(pcr - 1) = 0;
                    pcr = pminor;
                    while (0 != strncmp(pcr++, "\n", 1));
                    *(pcr - 1) = 0;
                    cdc_major = atoi((const char *)pmajor);
                    cdc_minor = atoi((const char *)pminor);
                    if (0 == stat(cdc_nod, &st)) {
                        if (st.st_rdev != (unsigned)MKDEV(cdc_major, cdc_minor)) {
                            need_newnod = 1;
                            if (0 != remove(cdc_nod)) {
                                LOGE("remove %s failed. errno = %d(%s)", cdc_nod, errno, strerror(errno));
                            }
                        } else {
                            need_newnod = 0;
                        }
                    } else {
                        need_newnod = 1;
                    }
                    if ((1 == need_newnod) && (0 != mknod(cdc_nod, S_IRUSR | S_IWUSR | S_IFCHR, MKDEV(cdc_major, cdc_minor)))) {
                        LOGE("mknod for %s failed, MAJOR = %d, MINOR =%d, errno = %d(%s)", cdc_nod, cdc_major,
                            cdc_minor, errno, strerror(errno));
                    }
                } else {
                    LOGE("major or minor get failed, uevent_buf = %s", uevent_buf);
                }
            }
        #endif
            break;
        }                   
    }
    closedir(pSubDir);
    
    return find_qmichannel;
}

int ql_find_qcqmi(char *dir)
{
    char subdir[MAX_PATH]={0};
    DIR *pSubDir;
    struct dirent* subent = NULL;
    int find_qmichannel = 0;

    strcpy(subdir, dir);
    strcat(subdir, "/GobiQMI");
    if ((pSubDir = opendir(subdir)) == NULL)  {    
       LOGE("Cannot open directory:%s/", subdir);
      return -ENODEV;
    }
    while ((subent = readdir(pSubDir)) != NULL) {
       if (strncmp(subent->d_name, "qcqmi", strlen("qcqmi")) == 0) {
           LOGD("Find qmichannel = %s", subent->d_name);
           find_qmichannel = 1;
           break;
       }                         
    }
    
    closedir(pSubDir);
    return find_qmichannel;
}

char* ql_find_usbnet_adapter(char *dir)
{
    char subdir[MAX_PATH]={0};
    DIR *pSubDir;
    struct dirent* subent = NULL;

    strcpy(subdir, dir);
    strcat(subdir, "/net");
    if ((pSubDir = opendir(subdir)) == NULL)  {    
        LOGE("Cannot open directory:%s/", subdir);
        return NULL;
    }
    while ((subent = readdir(pSubDir)) != NULL) {
        if ((strncmp(subent->d_name, "wwan", strlen("wwan")) == 0)
            || (strncmp(subent->d_name, "eth", strlen("eth")) == 0)
            || (strncmp(subent->d_name, "usb", strlen("usb")) == 0)) {
            static char s_pp_usbnet_adapter[32]={0};
            strcpy(s_pp_usbnet_adapter, subent->d_name);
            LOGD("Find usbnet_adapter = %s", s_pp_usbnet_adapter);
            closedir(pSubDir);
            return s_pp_usbnet_adapter;
        }
    }
    
    closedir(pSubDir);
    return NULL;
}


int ql_find_ndis(char *usb_class_name, struct ql_usb_device_info *pusb_device_info)
{
    struct dirent* ent = NULL;  
    struct dirent* subent = NULL; 
    char subdir[MAX_PATH]={0};
    DIR *pDir, *pSubDir;
    int find_qmichannel = 0;
    char dir[MAX_PATH] = {0};
    char *p_usbnet_adapter = NULL;
    strcpy(dir, pusb_device_info->usbdevice_pah);

    sprintf(subdir, ":1.%d", 4);
    strcat(dir, subdir);
    
    if ((pDir = opendir(dir)) == NULL)  {  
        LOGE("Cannot open directory:%s/", dir);  
        return -ENODEV;  
    }
    
    while ((ent = readdir(pDir)) != NULL) {
        if ((strlen(ent->d_name) == strlen(usb_class_name) && !strncmp(ent->d_name, usb_class_name, strlen(usb_class_name)))) {
            find_qmichannel = (1 == ql_make_node(dir, ent->d_name)) ? 1 : 0;
        } else if (strncmp(ent->d_name, "GobiQMI", strlen("GobiQMI")) == 0) {
            find_qmichannel = (1 == ql_find_qcqmi(dir)) ? 1 : 0;
        } else if (strncmp(ent->d_name, "net", strlen("net")) == 0) {
            p_usbnet_adapter = ql_find_usbnet_adapter(dir);
        }

        if (find_qmichannel && p_usbnet_adapter)
        {
            strcpy(pusb_device_info->ttyndis_name, p_usbnet_adapter);
            break;
        }
    }
    closedir(pDir);     

    return (find_qmichannel && p_usbnet_adapter) ? 0 : -1;

}

static int ql_get_usbnet_adapter(struct ql_usb_device_info *pusb_device_info)
{
    char dir[MAX_PATH]={0};
    int fd;
    int find_usb_device = 0;
    int find_qmichannel = 0;

    char *usb_class_name = NULL;

    int i = 0;

    usb_class_name = ql_get_usb_class_name();

    do {
        if(!ql_find_ndis(usb_class_name, pusb_device_info)) {
            return 0;
        }

    } while(0); //just for one card

    return -1;
}


int ql_get_ndisname(char **pp_usbnet_adapter)
{
    int cnt = ql_get_usb_device_info(USB_AT_INF);
        
    if(cnt > 0) {
        *pp_usbnet_adapter = (0 == s_usb_device_info[0].ttyndis_name[0]) ? NULL : s_usb_device_info[0].ttyndis_name;
        return 0;
    }
    
    *pp_usbnet_adapter = NULL;
    return -1;
}

void ql_set_autosuspend(int enable)
{
    char dir[MAX_PATH];
    int index = 0;

    memset(dir, 0, sizeof(dir));

    int cnt = ql_get_usb_device_info(USB_AT_INF);

    do {
        if (s_usb_device_info[index].usbdevice_pah[0]) {
            char shell_command[MAX_PATH+32];
            snprintf(shell_command, sizeof(shell_command), "echo %s > %s/power/control", enable ? "auto" : "on", s_usb_device_info[index].usbdevice_pah);
            system(shell_command);
            LOGD("%s", shell_command);
            LOGD("%s %s", __func__, enable ? "auto" : "off");
        }

        index++;
    } while(index < MAX_CARD_NUM);
}
