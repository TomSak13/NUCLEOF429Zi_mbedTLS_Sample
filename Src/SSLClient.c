
#include "lwip/dhcp.h"
#include "lwip/tcpip.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
   
#include "netif/ethernet.h"

#include "ethernetif.h"

#include "mbedtls_config.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"
#include "mbedtls/memory_buffer_alloc.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy_poll.h"

#include "stm32f4xx_nucleo_144.h"
#include <string.h>

#include "cmsis_os.h"

#include "SSLClient.h"


#define SERVER_PORT "4433"
#define SERVER_NAME "192.168.11.11" // 接続先のSSLサーバーを起動するPCのIPアドレスに修正してください

#define GET_REQUEST "GET / HTTP/1.0\r\n\r\n"

static struct netif netif;

#ifdef MBEDTLS_MEMORY_BUFFER_ALLOC_C
/* heapではなく、mbed-TLS内部のアロケータによってメモリ確保をする場合の、メモリ確保領域として使用 */
uint8_t memory_buf[100000];
#endif

static void LwIPInit()
{
    ip4_addr_t addr;
    ip4_addr_t netmask;
    ip4_addr_t gw;

    tcpip_init(NULL, NULL);

    ip_addr_set_zero_ip4(&addr);
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gw);
  
    netif_add(&netif, &addr, &netmask, &gw, NULL, &ethernetif_init, &ethernet_input);
    netif_set_default(&netif);

    /* サンプルなのでEthernetは起動前から繋がっている前提 */
    if (netif_is_link_up(&netif))
    {
        netif_set_up(&netif);
    }
    else
    {
        /* Ethernetが繋がっていなければエラーとする */
        BSP_LED_Init(LED2);
        while (1)
        {
            BSP_LED_Toggle(LED2);
            osDelay(100);
        }
    }
}

static void DHCPClient()
{
    dhcp_start(&netif);
    while (!dhcp_supplied_address(&netif))
    {
        osDelay(100);
    }
}

void SSLClient(void const * argument)
{
    int len;
    int ret;

    mbedtls_net_context server_fd;
    uint8_t buf[1024];
    const uint8_t *pers = (uint8_t *)("ssl_client");

    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;

    #ifdef MBEDTLS_MEMORY_BUFFER_ALLOC_C
    /* heapではなく、mbed-TLS内部のアロケータによってメモリ確保をする場合のメモリ確保領域としてmemory_bufを指定する場合 */
    mbedtls_memory_buffer_alloc_init(memory_buf, sizeof(memory_buf));
    #endif

    /* LwIP初期化。DHCPのIPアドレス取得まで行う  */
    LwIPInit();
    DHCPClient();

    /* mbed-TLS初期化 */
    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_x509_crt_init(&cacert);
    mbedtls_ctr_drbg_init(&ctr_drbg);

    mbedtls_entropy_init(&entropy);
    len = strlen((char *)pers);
    if (mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *) pers, len) != 0)
    {
        goto EXIT_TLS;
    }

    /* 証明書の初期化(自身の鍵・証明書設定) */
    if (mbedtls_x509_crt_parse(&cacert, (const unsigned char *) mbedtls_test_cas_pem, mbedtls_test_cas_pem_len) < 0)
    {
        goto EXIT_TLS;
    }

    /* TCPのソケットとして接続(TLSの通信はしない) */
    if (mbedtls_net_connect(&server_fd, SERVER_NAME, SERVER_PORT, MBEDTLS_NET_PROTO_TCP) != 0)
    {
        goto EXIT_TLS;
    }
    /* クライアントとしてmbedTLSを設定 */
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) != 0)
    {
        goto EXIT_TLS;
    }

    /* 通信モードをconfに設定。MBEDTLS_SSL_VERIFY_OPTIONALなので、エラー内容を「mbedtls_ssl_get_verify_result」で確認する必要がある */
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);

    /* mbedtls_x509_crt_parseで設定した証明書・鍵をconfに設定 */
    mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);

    /* 乱数生成設定と生成コールバックをconfに設定 */
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

    /* confに設定したパラメータを反映 */
    if (mbedtls_ssl_setup(&ssl, &conf) != 0)
    {
        goto EXIT_TLS;
    }

    /* HOST名指定 */
    if (mbedtls_ssl_set_hostname(&ssl, "localhost") != 0)
    {
        goto EXIT_TLS;
    }

    /* データの送受信に使う関数を「mbedtls_net_send」「mbedtls_net_recv」にて設定。server_fdは送受信関数呼び出し時に引数として指定される */
    mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

    /* TLSの4WAYハンドシェイクを実施 */
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            goto EXIT_TLS;
        }
    }

    /* ４WAYハンドシェイクの結果を確認 */
    if (mbedtls_ssl_get_verify_result( &ssl ) != 0)
    {
        goto EXIT_TLS;
    }

    /* HTTP のGETリクエストを送る */
    snprintf((char *)buf, sizeof(buf) / sizeof(char), GET_REQUEST);
    len = strlen((char *) buf);
    while ((ret = mbedtls_ssl_write(&ssl, buf, len)) <= 0)
    {
        if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            goto EXIT_TLS;
        }
    }

    /* HTTPのレスポンスを解析 */
    do
    {
        len = sizeof(buf) - 1;
        memset(buf, 0, sizeof(buf));
        ret = mbedtls_ssl_read(&ssl, buf, len);

        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
        {
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY)
        {
            break;
        }
        if (ret < 0)
        {
            break;
        }
        if (ret == 0)
        {
            break;
        }
    }
    while(1);

    /* 通信終了 */
    mbedtls_ssl_close_notify(&ssl);

EXIT_TLS:
    /* リソース開放 */
    mbedtls_net_free(&server_fd);
    mbedtls_x509_crt_free(&cacert);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctr_drbg);
    mbedtls_entropy_free(&entropy);

    if ((ret < 0) && (ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY))
    {
        /* error */
        BSP_LED_Init(LED2);
        BSP_LED_Toggle(LED2);
        return;
    }

    BSP_LED_Init(LED1);
    while (1)
    {
        BSP_LED_Toggle(LED1);
        osDelay(200);
    }
}
