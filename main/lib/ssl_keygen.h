#ifndef SSL_KEYGEN_H
#define SSL_KEYGEN_H

#include <cstring>
#include <cerrno>


#include "mbedtls/x509_csr.h"
#include "spiffs_manager.h"

#define SSL_CERT_BUFFER_LEN 4096

class SSLCertGenerator {
public:
    SSLCertGenerator();
    ~SSLCertGenerator();
    bool generate_server_key(char * sta_ipv4_addr, char *ap_ipv4_addr);
    bool generate_root_ca(mbedtls_x509_time valid_from, mbedtls_x509_time valid_to);
    bool rootca_exists();
    char *rootca_cert();
    void wipe_certs();
    char server_key[SSL_CERT_BUFFER_LEN] = "";
    char server_cert[SSL_CERT_BUFFER_LEN] = "";
private:
    char ca_cert[SSL_CERT_BUFFER_LEN] = "";
    char ca_key[SSL_CERT_BUFFER_LEN] = "";
    SPIFFSManager *spiffs_manager;
    int parse_serial_decimal_format(unsigned char *obuf, size_t obufmax,
                                const char *ibuf, size_t *len);
    bool add_san_item(mbedtls_x509_san_list **head, mbedtls_x509_san_list **latest, char *type, char *value);
    static constexpr const char * const TAG = "ssl_keygen";
    const char * ca_cert_filename = "/root_ca.crt";
    const char * ca_key_filename = "/root_ca.key";
};

#endif