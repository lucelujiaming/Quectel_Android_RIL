#ifndef __QL_USB_H__
#define __QL_USB_H__

#define MAX_PATH 256
#define MAX_CARD_NUM 4

#define USB_AT_INF 0
#define USB_PPP_INF 1


#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#endif
    
struct ql_usb_device_info {
    int idVendor;
    int idProduct;
    char usb_device_name[MAX_PATH];
    char usbdevice_pah[MAX_PATH];
    char ttyAT_name[MAX_PATH];
    char ttyndis_name[MAX_PATH];
};

struct ql_usb_device_info s_usb_device_info[MAX_CARD_NUM];

int is_usb_match(unsigned short vid, unsigned short pid);

char *ql_get_ttyname(int usb_interface, char *out_ttyname);
int ql_get_ndisname(char **pp_usbnet_adapter);
int ql_get_usb_device_info(int usb_interface);
void ql_set_autosuspend(int enable);

#endif

