

void RNG_Init(void);
int mbedtls_hardware_poll(void *Data, unsigned char *Output, size_t Len, size_t *oLen);
void ConfigureClockForRNG(void);
