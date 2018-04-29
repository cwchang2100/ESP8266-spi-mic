#ifndef _GSENSOR_H_
#define _GSENSOR_H_

void gsensor_init(void);
void gsensr_cmd_read(void);
bool user_gsensor_read_x(uint16 *x);
bool user_gsensor_read_y(uint16 *y);
bool user_gsensor_read_z(uint16 *z);

#endif