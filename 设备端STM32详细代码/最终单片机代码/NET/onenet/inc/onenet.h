#ifndef _ONENET_H_
#define _ONENET_H_





_Bool OneNET_RegisterDevice(void);

_Bool OneNet_DevLink(void);

void OneNet_SendData(u8 temperature, u8 humidity);

void OneNET_Subscribe(void);

void OneNet_RevPro(unsigned char *cmd);

#endif
