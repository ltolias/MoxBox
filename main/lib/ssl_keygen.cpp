

#include "ssl_keygen.h"
#include "esp_log.h"

#include "mbedtls/build_info.h"
#include "mbedtls/platform.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"




SSLCertGenerator::SSLCertGenerator() {
    spiffs_manager = new SPIFFSManager("certs", true);

    if (rootca_exists()) {
        ESP_LOGI(TAG, "loading existing root ca cert/key\n");
        String cert_file = spiffs_manager->read_file(ca_cert_filename);
        strncpy((char *)ca_cert, cert_file.c_str(), cert_file.length() + 1);

        String key_file = spiffs_manager->read_file(ca_key_filename);
        strncpy((char *)ca_key, key_file.c_str(), key_file.length() + 1);

    } else {
        ESP_LOGI(TAG, "No cached root ca\n");
    }

    //delete all certs at 32k free of 256k
    if (spiffs_manager->get_fs()->totalBytes() - spiffs_manager->get_fs()->usedBytes() < 32768) { 
        ESP_LOGI(TAG, "Certificate cache nearly full - cleaning all server certs (rooca certs will not be deleted)\n");
        File root = spiffs_manager->get_fs()->open("/");
        File file = root.openNextFile();
        while (file) {
            if (strcmp(file.path(), ca_cert_filename) != 0 && strcmp(file.path(), ca_key_filename) != 0) {
                spiffs_manager->delete_file(file.path());
            }
            file = root.openNextFile();
        }
    }
}
SSLCertGenerator::~SSLCertGenerator() {
    delete spiffs_manager;
}

void SSLCertGenerator::wipe_certs() {
    //note: this deletes rootca as well
    spiffs_manager->get_fs()->format();
}

bool SSLCertGenerator::generate_server_key(char * sta_ipv4_addr, char * ap_ipv4_addr) {
    
    int fname_buflen = 64;
    char cert_filename_buf[fname_buflen];
    char key_filename_buf[fname_buflen];

    if (strlen(sta_ipv4_addr) >= 7) {
        snprintf(cert_filename_buf, fname_buflen, "/%s-%s.crt", sta_ipv4_addr, ap_ipv4_addr);
        snprintf(key_filename_buf, fname_buflen, "/%s-%s.key", sta_ipv4_addr, ap_ipv4_addr);
    } else {
        snprintf(cert_filename_buf, fname_buflen, "/%s.crt", ap_ipv4_addr);
        snprintf(key_filename_buf, fname_buflen, "/%s.key", ap_ipv4_addr);
    }
    ESP_LOGI(TAG, "Checking for existing cert/key...\n");
    if (spiffs_manager->exists_file(cert_filename_buf) && spiffs_manager->exists_file(key_filename_buf)) {
        ESP_LOGI(TAG, "loading existing ssl cert...\n");
        String cert_file = spiffs_manager->read_file(cert_filename_buf);
        strncpy((char *)server_cert, cert_file.c_str(), cert_file.length() + 1);

        String key_file = spiffs_manager->read_file(key_filename_buf);
        strncpy((char *)server_key, key_file.c_str(), key_file.length() + 1);

        return MBEDTLS_EXIT_SUCCESS;
    } else {
        ESP_LOGI(TAG, "No cached certs for ips %s,%s creating now...\n", sta_ipv4_addr, ap_ipv4_addr);
    }
    
    char issuer_name[64];
    char subject_name[64];
    snprintf(issuer_name, 64, "CN=%s.local", MDNS_NAME);
    snprintf(subject_name, 64, "CN=%s.local", MDNS_NAME);

    // get validity period from ca cert and use for server cert
    mbedtls_x509_crt ca_cert_mbedtls;
    mbedtls_x509_crt_init(&ca_cert_mbedtls);
    mbedtls_x509_crt_parse(&ca_cert_mbedtls, (const unsigned char *)ca_cert, strlen(ca_cert) + 1);
    char not_before[32];
    char not_after[32];
    snprintf(not_before, 32, "%04d%02d%02d%02d%02d%02d",
                           ca_cert_mbedtls.valid_from.year, ca_cert_mbedtls.valid_from.mon,
                           ca_cert_mbedtls.valid_from.day,  ca_cert_mbedtls.valid_from.hour,
                           ca_cert_mbedtls.valid_from.min,  ca_cert_mbedtls.valid_from.sec);
    snprintf(not_after, 32, "%04d%02d%02d%02d%02d%02d",
                           ca_cert_mbedtls.valid_to.year, ca_cert_mbedtls.valid_to.mon,
                           ca_cert_mbedtls.valid_to.day,  ca_cert_mbedtls.valid_to.hour,
                           ca_cert_mbedtls.valid_to.min,  ca_cert_mbedtls.valid_to.sec);

    mbedtls_x509_crt_free(&ca_cert_mbedtls);

    unsigned int key_size_bits = 2048;


    char san_dns_1[64];
    char san_dns_2[64];
    snprintf(san_dns_1, 64, "%s.local", MDNS_NAME);
    snprintf(san_dns_2, 64, "www.%s.local", MDNS_NAME);
    
    mbedtls_x509_san_list *san_head = NULL;
    mbedtls_x509_san_list *san_latest = NULL;
    bool success = add_san_item(&san_head, &san_latest, "DNS", san_dns_1);
    success &= add_san_item(&san_head, &san_latest, "DNS", san_dns_2);
    if (strlen(sta_ipv4_addr) >= 7) {
        success &= add_san_item(&san_head, &san_latest, "IP", sta_ipv4_addr);
    }
    success &= add_san_item(&san_head, &san_latest, "IP", ap_ipv4_addr);

    if (!success) {
      ESP_LOGI(TAG, " failed to create san list");
      return MBEDTLS_EXIT_FAILURE; //todo free san list
    }

    int version = MBEDTLS_X509_CRT_VERSION_3; //default but what are these versions of?
    mbedtls_md_type_t digest_algo = MBEDTLS_MD_SHA256;


    int ret = 1;
    int exit_code = MBEDTLS_EXIT_FAILURE;
    mbedtls_pk_context server_key_context;
    mbedtls_entropy_context entropy_server_key;
    mbedtls_ctr_drbg_context ctr_drbg_server_key;
    const unsigned char *pers_server = (const unsigned char *) "gen_key";
    mbedtls_entropy_init(&entropy_server_key);
    mbedtls_pk_init(&server_key_context);
    mbedtls_ctr_drbg_init(&ctr_drbg_server_key);

    #if defined(MBEDTLS_USE_PSA_CRYPTO)
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        mbedtls_fprintf(stderr, "Failed to initialize PSA Crypto implementation: %d\n",
                        (int) status);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key;
    }
    #endif /* MBEDTLS_USE_PSA_CRYPTO */


    mbedtls_pk_context loaded_issuer_key;
    mbedtls_pk_context *issuer_key = &loaded_issuer_key,
                       *subject_key = &server_key_context;

    int i;
    char *p, *q, *r;
    mbedtls_x509write_cert generated_crt;
    unsigned char serial[MBEDTLS_X509_RFC5280_MAX_SERIAL_LEN];
    mbedtls_asn1_sequence *ext_key_usage;
    mbedtls_entropy_context entropy_csr;
    mbedtls_ctr_drbg_context ctr_drbg_csr;
    const char *pers = "crt example app";
    uint8_t ip[4] = { 0 };
    /*
     * Set to sane values
     */


    mbedtls_x509write_crt_init(&generated_crt);
    mbedtls_pk_init(&loaded_issuer_key);
    mbedtls_ctr_drbg_init(&ctr_drbg_csr);
    mbedtls_entropy_init(&entropy_csr);
    memset(serial, 0, sizeof(serial));



    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg_server_key, mbedtls_entropy_func, &entropy_server_key, pers_server, strlen((char*)pers_server))) != 0) {
        ESP_LOGI(TAG, " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key;
    }

      /*
     * 1.1. Generate the key
     */
    if ((ret = mbedtls_pk_setup(&server_key_context, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0) {
        ESP_LOGI(TAG, " failed\n  !  mbedtls_pk_setup returned -0x%04x\n", (unsigned int) -ret);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key;
    }

    ESP_LOGI(TAG, "Generate new rsa key\n");
    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(server_key_context), mbedtls_ctr_drbg_random, &ctr_drbg_server_key, key_size_bits, 65537);
    if (ret != 0) {
        mbedtls_printf(" failed\n  !  mbedtls_rsa_gen_key returned -0x%04x\n",
                        (unsigned int) -ret);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key;
    }

    mbedtls_pk_write_key_pem(&server_key_context, (unsigned char *) server_key, SSL_CERT_BUFFER_LEN);

    //finished generating server key


    /*
     * 0. Seed the PRNG
     */
    ESP_LOGI(TAG, "  . Seeding the random number generator...");
    
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg_csr, mbedtls_entropy_func, &entropy_csr,
                                     (const unsigned char *) pers,
                                     strlen(pers))) != 0) {
        ESP_LOGI(TAG, " failed\n  !  mbedtls_ctr_drbg_seed\n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit;
    }

    ESP_LOGI(TAG, "  . Reading serial number...");

    size_t serial_len;
    ret = parse_serial_decimal_format(serial, sizeof(serial), "1", &serial_len);
    

    if (ret != 0) {
        ESP_LOGI(TAG, " failed\n  !  Unable to parse serial\n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit;
    }

    ESP_LOGI(TAG, "  . Loading the issuer key ...");
  
    //todo: char * to unsigned char * cast?    
    ret = mbedtls_pk_parse_key(&loaded_issuer_key, (const unsigned char *) ca_key, strlen(ca_key) + 1, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg_csr);

    ESP_LOGI(TAG, "  . Setting the subject key ...");
  
    mbedtls_x509write_crt_set_subject_key(&generated_crt, subject_key);

    ESP_LOGI(TAG, "  . Setting the issuer key ...");
  
    mbedtls_x509write_crt_set_issuer_key(&generated_crt, issuer_key);

    ESP_LOGI(TAG, "  . Setting subject name ...");
  
    /*
     * 1.0. Check the names for validity
     */
    if ((ret = mbedtls_x509write_crt_set_subject_name(&generated_crt, subject_name)) != 0) {
        
        ESP_LOGI(TAG, " failed\n  !  mbedtls_x509write_crt_set_subject_name \n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit;
    }


    if ((ret = mbedtls_x509write_crt_set_issuer_name(&generated_crt, issuer_name)) != 0) {
        ESP_LOGI(TAG, " failed\n  !  mbedtls_x509write_crt_set_issuer_name \n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit;
    }


    ESP_LOGI(TAG, "  . Setting certificate values ...\n");

    mbedtls_x509write_crt_set_version(&generated_crt, version);


    mbedtls_x509write_crt_set_md_alg(&generated_crt, digest_algo);


    ret = mbedtls_x509write_crt_set_serial_raw(&generated_crt, serial, serial_len);


    ret = mbedtls_x509write_crt_set_validity(&generated_crt, not_before, not_after);


    if (version == MBEDTLS_X509_CRT_VERSION_3) {
        ESP_LOGI(TAG, "  . Adding the Basic Constraints extension ...");
        ret = mbedtls_x509write_crt_set_basic_constraints(&generated_crt, true, -1); //todo: I think this should actually be false/0 since this cert is not self signed but need to test
    }

    #if defined(MBEDTLS_MD_CAN_SHA1)
    if (version == MBEDTLS_X509_CRT_VERSION_3) {
        ret = mbedtls_x509write_crt_set_subject_key_identifier(&generated_crt);
        ret = mbedtls_x509write_crt_set_authority_key_identifier(&generated_crt);
    }
    #endif /* MBEDTLS_MD_CAN_SHA1 */

    // if (opt.version == MBEDTLS_X509_CRT_VERSION_3) {
    //     ret = mbedtls_x509write_crt_set_key_usage(&generated_crt, 0);
    //     ESP_LOGI(TAG, " ok\n");
    // }


    ret = mbedtls_x509write_crt_set_subject_alternative_name(&generated_crt, san_head);

    // if (opt.ext_key_usage) {
    //     ret = mbedtls_x509write_crt_set_ext_key_usage(&generated_crt, opt.ext_key_usage);
    // }
    // if (opt.version == MBEDTLS_X509_CRT_VERSION_3) {
    //     ret = mbedtls_x509write_crt_set_ns_cert_type(&generated_crt, opt.ns_cert_type);
    // }

    /*
     * 1.2. Writing the certificate
     */
    ESP_LOGI(TAG, "  . Writing the certificate...\n");

    ret = mbedtls_x509write_crt_pem(&generated_crt, (unsigned char *) server_cert, SSL_CERT_BUFFER_LEN, mbedtls_ctr_drbg_random, &ctr_drbg_csr);


    ESP_LOGI(TAG, "Saving to spiffs...\n");
    if (spiffs_manager->exists_file(cert_filename_buf)) {
        spiffs_manager->delete_file(cert_filename_buf);
    } 
    spiffs_manager->write_file(cert_filename_buf, (char *)server_cert);

    if (spiffs_manager->exists_file(key_filename_buf)) {
        spiffs_manager->delete_file(key_filename_buf);
    }
    spiffs_manager->write_file(key_filename_buf, (char *)server_key);


    
    exit_code = MBEDTLS_EXIT_SUCCESS;

    exit:
        mbedtls_x509write_crt_free(&generated_crt);
      mbedtls_pk_free(&loaded_issuer_key);
      mbedtls_ctr_drbg_free(&ctr_drbg_csr);
      mbedtls_entropy_free(&entropy_csr);
      //todo: free san list (san_head)
  #if defined(MBEDTLS_USE_PSA_CRYPTO)
      mbedtls_psa_crypto_free();
  #endif /* MBEDTLS_USE_PSA_CRYPTO */
  exit_server_key:
      mbedtls_pk_free(&server_key_context);
      mbedtls_ctr_drbg_free(&ctr_drbg_server_key);
      mbedtls_entropy_free(&entropy_server_key);
      #if defined(MBEDTLS_USE_PSA_CRYPTO)
      mbedtls_psa_crypto_free();
      #endif /* MBEDTLS_USE_PSA_CRYPTO */

    return exit_code;


}

bool SSLCertGenerator::rootca_exists() {
     return spiffs_manager->exists_file(ca_cert_filename) && spiffs_manager->exists_file(ca_key_filename);
}
char * SSLCertGenerator::rootca_cert() {
     return ca_cert;
}

bool SSLCertGenerator::generate_root_ca(mbedtls_x509_time valid_from, mbedtls_x509_time valid_to) {
  
    char issuer_name[64];
    char subject_name[64];
    snprintf(issuer_name, 64, "CN=%s.local", MDNS_NAME);
    snprintf(subject_name, 64, "CN=%s.local", MDNS_NAME);

    char not_before[32];
    char not_after[32];
    snprintf(not_before, 32, "%04d%02d%02d%02d%02d%02d",
                           valid_from.year, valid_from.mon,
                           valid_from.day,  valid_from.hour,
                           valid_from.min,  valid_from.sec);
    snprintf(not_after, 32, "%04d%02d%02d%02d%02d%02d",
                           valid_to.year, valid_to.mon,
                           valid_to.day,  valid_to.hour,
                           valid_to.min,  valid_to.sec);

    unsigned int key_size_bits = 2048;

    char san_dns_1[64];
    char san_dns_2[64];
    snprintf(san_dns_1, 64, "%s.local", MDNS_NAME);
    snprintf(san_dns_2, 64, "www.%s.local", MDNS_NAME);
    
    mbedtls_x509_san_list *san_head = NULL;
    mbedtls_x509_san_list *san_latest = NULL;
    bool success = add_san_item(&san_head, &san_latest, "DNS", san_dns_1);
    success &= add_san_item(&san_head, &san_latest, "DNS", san_dns_2);

    if (!success) {
      ESP_LOGI(TAG, " failed to create san list");
      return MBEDTLS_EXIT_FAILURE; //todo free san list
    }

    int version = MBEDTLS_X509_CRT_VERSION_3; 
    mbedtls_md_type_t digest_algo = MBEDTLS_MD_SHA256;


    int ret = 1;
    int exit_code = MBEDTLS_EXIT_FAILURE;
    mbedtls_pk_context server_key_context;
    mbedtls_entropy_context entropy_server_key;
    mbedtls_ctr_drbg_context ctr_drbg_server_key;
    const unsigned char *pers_server = (const unsigned char *) "gen_key";
    mbedtls_entropy_init(&entropy_server_key);
    mbedtls_pk_init(&server_key_context);
    mbedtls_ctr_drbg_init(&ctr_drbg_server_key);

    #if defined(MBEDTLS_USE_PSA_CRYPTO)
    psa_status_t status = psa_crypto_init();
    if (status != PSA_SUCCESS) {
        mbedtls_fprintf(stderr, "Failed to initialize PSA Crypto implementation: %d\n",
                        (int) status);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key_ca;
    }
    #endif /* MBEDTLS_USE_PSA_CRYPTO */


    mbedtls_pk_context loaded_issuer_key;
    mbedtls_pk_context *issuer_key = &loaded_issuer_key,
                       *subject_key = &server_key_context;

    int i;
    char *p, *q, *r;
    mbedtls_x509write_cert generated_crt;
    unsigned char serial[MBEDTLS_X509_RFC5280_MAX_SERIAL_LEN];
    mbedtls_asn1_sequence *ext_key_usage;
    mbedtls_entropy_context entropy_csr;
    mbedtls_ctr_drbg_context ctr_drbg_csr;
    const char *pers = "crt example app";
    uint8_t ip[4] = { 0 };
    /*
     * Set to sane values
     */


    mbedtls_x509write_crt_init(&generated_crt);
    mbedtls_pk_init(&loaded_issuer_key);
    mbedtls_ctr_drbg_init(&ctr_drbg_csr);
    mbedtls_entropy_init(&entropy_csr);
    memset(serial, 0, sizeof(serial));



    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg_server_key, mbedtls_entropy_func, &entropy_server_key, pers_server, strlen((char*)pers_server))) != 0) {
        ESP_LOGI(TAG, " failed\n  ! mbedtls_ctr_drbg_seed returned %d\n", ret);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key_ca;
    }

      /*
     * 1.1. Generate the key
     */
    if ((ret = mbedtls_pk_setup(&server_key_context, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA))) != 0) {
        ESP_LOGI(TAG, " failed\n  !  mbedtls_pk_setup returned -0x%04x\n", (unsigned int) -ret);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key_ca;
    }

    ESP_LOGI(TAG, "Generate new rsa key\n");
    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(server_key_context), mbedtls_ctr_drbg_random, &ctr_drbg_server_key, key_size_bits, 65537);
    if (ret != 0) {
        mbedtls_printf(" failed\n  !  mbedtls_rsa_gen_key returned -0x%04x\n",
                        (unsigned int) -ret);
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_server_key_ca;
    }

    mbedtls_pk_write_key_pem(&server_key_context, (unsigned char *) ca_key, SSL_CERT_BUFFER_LEN);

    //finished generating server key


    /*
     * 0. Seed the PRNG
     */
    ESP_LOGI(TAG, "  . Seeding the random number generator...");
    
    if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg_csr, mbedtls_entropy_func, &entropy_csr,
                                     (const unsigned char *) pers,
                                     strlen(pers))) != 0) {
        ESP_LOGI(TAG, " failed\n  !  mbedtls_ctr_drbg_seed\n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_ca;
    }

    ESP_LOGI(TAG, "  . Reading serial number...");

    size_t serial_len;
    ret = parse_serial_decimal_format(serial, sizeof(serial), "1", &serial_len);
    

    if (ret != 0) {
        ESP_LOGI(TAG, " failed\n  !  Unable to parse serial\n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_ca;
    }

    ESP_LOGI(TAG, "  . Loading the issuer key ...");
  
    // sign with our own key we just created
    ret = mbedtls_pk_parse_key(&loaded_issuer_key, (const unsigned char *) ca_key, strlen(ca_key) + 1, NULL, 0, mbedtls_ctr_drbg_random, &ctr_drbg_csr);

    ESP_LOGI(TAG, "  . Setting the subject key ...");
  
    mbedtls_x509write_crt_set_subject_key(&generated_crt, subject_key);

    ESP_LOGI(TAG, "  . Setting the issuer key ...");
  
    mbedtls_x509write_crt_set_issuer_key(&generated_crt, issuer_key);

    ESP_LOGI(TAG, "  . Setting subject name ...");
  
    /*
     * 1.0. Check the names for validity
     */
    if ((ret = mbedtls_x509write_crt_set_subject_name(&generated_crt, subject_name)) != 0) {
        
        ESP_LOGI(TAG, " failed\n  !  mbedtls_x509write_crt_set_subject_name \n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_ca;
    }


    if ((ret = mbedtls_x509write_crt_set_issuer_name(&generated_crt, issuer_name)) != 0) {
        ESP_LOGI(TAG, " failed\n  !  mbedtls_x509write_crt_set_issuer_name \n");
        exit_code = MBEDTLS_EXIT_FAILURE;
        goto exit_ca;
    }


    ESP_LOGI(TAG, "  . Setting certificate values ...\n");

    mbedtls_x509write_crt_set_version(&generated_crt, version);


    mbedtls_x509write_crt_set_md_alg(&generated_crt, digest_algo);


    ret = mbedtls_x509write_crt_set_serial_raw(&generated_crt, serial, serial_len);


    ret = mbedtls_x509write_crt_set_validity(&generated_crt, not_before, not_after);

    if (version == MBEDTLS_X509_CRT_VERSION_3) {
        ESP_LOGI(TAG, "  . Adding the Basic Constraints extension and setting ca_istrue=true and max_pathlen=-1 (unlimited)");
        ret = mbedtls_x509write_crt_set_basic_constraints(&generated_crt, true, -1);
    }

    #if defined(MBEDTLS_MD_CAN_SHA1)
    if (version == MBEDTLS_X509_CRT_VERSION_3) {
        ret = mbedtls_x509write_crt_set_subject_key_identifier(&generated_crt);
        ret = mbedtls_x509write_crt_set_authority_key_identifier(&generated_crt);
    }
    #endif /* MBEDTLS_MD_CAN_SHA1 */

    // if (opt.version == MBEDTLS_X509_CRT_VERSION_3) {
    //     ret = mbedtls_x509write_crt_set_key_usage(&generated_crt, 0);
    //     ESP_LOGI(TAG, " ok\n");
    // }

 
    ret = mbedtls_x509write_crt_set_subject_alternative_name(&generated_crt, san_head);

    // if (opt.ext_key_usage) {
    //     ret = mbedtls_x509write_crt_set_ext_key_usage(&generated_crt, opt.ext_key_usage);
    // }
    // if (opt.version == MBEDTLS_X509_CRT_VERSION_3) {
    //     ret = mbedtls_x509write_crt_set_ns_cert_type(&generated_crt, opt.ns_cert_type);
    // }

    /*
     * 1.2. Writing the certificate
     */
    ESP_LOGI(TAG, "  . Writing the certificate...\n");

    ret = mbedtls_x509write_crt_pem(&generated_crt, (unsigned char *) ca_cert, SSL_CERT_BUFFER_LEN, mbedtls_ctr_drbg_random, &ctr_drbg_csr);


    ESP_LOGI(TAG, "Saving to spiffs...\n");
    if (spiffs_manager->exists_file(ca_cert_filename)) {
        spiffs_manager->delete_file(ca_cert_filename);
    } 
    spiffs_manager->write_file(ca_cert_filename, (char *)ca_cert);

    if (spiffs_manager->exists_file(ca_key_filename)) {
        spiffs_manager->delete_file(ca_key_filename);
    }
    spiffs_manager->write_file(ca_key_filename, (char *)ca_key);


    
    exit_code = MBEDTLS_EXIT_SUCCESS;

    exit_ca:
        mbedtls_x509write_crt_free(&generated_crt);
        mbedtls_pk_free(&loaded_issuer_key);
        mbedtls_ctr_drbg_free(&ctr_drbg_csr);
        mbedtls_entropy_free(&entropy_csr);
        //todo: free san list (san_head)
        #if defined(MBEDTLS_USE_PSA_CRYPTO)
            mbedtls_psa_crypto_free();
        #endif /* MBEDTLS_USE_PSA_CRYPTO */
    
    exit_server_key_ca:
        mbedtls_pk_free(&server_key_context);
        mbedtls_ctr_drbg_free(&ctr_drbg_server_key);
        mbedtls_entropy_free(&entropy_server_key);
        #if defined(MBEDTLS_USE_PSA_CRYPTO)
            mbedtls_psa_crypto_free();
        #endif /* MBEDTLS_USE_PSA_CRYPTO */

    return exit_code;


}



bool SSLCertGenerator::add_san_item(mbedtls_x509_san_list **head, mbedtls_x509_san_list **latest, char *type, char *value) {

  mbedtls_x509_san_list *new_san = (mbedtls_x509_san_list*) mbedtls_calloc(1, sizeof(mbedtls_x509_san_list));
  if (!new_san) {
    return false;
  }
  new_san->next = NULL;

  if (!(*head))
    *head = new_san;

  if (*latest)
    (*latest)->next = new_san;

  (*latest) = new_san;
 
  uint8_t *ip = new uint8_t[4];

  if (strcmp(type, "RFC822") == 0) {
      (*latest)->node.type = MBEDTLS_X509_SAN_RFC822_NAME;
  } else if (strcmp(type, "URI") == 0) {
      (*latest)->node.type = MBEDTLS_X509_SAN_UNIFORM_RESOURCE_IDENTIFIER;
  } else if (strcmp(type, "DNS") == 0) {
      (*latest)->node.type = MBEDTLS_X509_SAN_DNS_NAME;
  } else if (strcmp(type, "IP") == 0) {
      size_t ip_addr_len = 0;
      (*latest)->node.type = MBEDTLS_X509_SAN_IP_ADDRESS;
      ip_addr_len = mbedtls_x509_crt_parse_cn_inet_pton(value, ip);
      if (ip_addr_len == 0) {
          mbedtls_printf("mbedtls_x509_crt_parse_cn_inet_pton failed to parse %s\n",
                          value);
          return false;
      }
      unsigned char * ip_uc = (unsigned char *) ip;
      (*latest)->node.san.unstructured_name.p = (unsigned char *) ip;
      (*latest)->node.san.unstructured_name.len = 4; //todo: make this work for ipv6 (parse_cn_inet_pton above does so...)
  } 
  if ((*latest)->node.type == MBEDTLS_X509_SAN_RFC822_NAME ||
      (*latest)->node.type == MBEDTLS_X509_SAN_UNIFORM_RESOURCE_IDENTIFIER ||
      (*latest)->node.type == MBEDTLS_X509_SAN_DNS_NAME) {
      (*latest)->node.san.unstructured_name.p = (unsigned char *) value;
      (*latest)->node.san.unstructured_name.len = strlen(value);
  }
  return true;

}



int SSLCertGenerator::parse_serial_decimal_format(unsigned char *obuf, size_t obufmax,
                                const char *ibuf, size_t *len)
{
    unsigned long long int dec;
    unsigned int remaining_bytes = sizeof(dec);
    unsigned char *p = obuf;
    unsigned char val;
    char *end_ptr = NULL;

    errno = 0;
    dec = strtoull(ibuf, &end_ptr, 10);

    if ((errno != 0) || (end_ptr == ibuf)) {
        return -1;
    }

    *len = 0;

    while (remaining_bytes > 0) {
        if (obufmax < (*len + 1)) {
            return -1;
        }

        val = (dec >> ((remaining_bytes - 1) * 8)) & 0xFF;

        /* Skip leading zeros */
        if ((val != 0) || (*len != 0)) {
            *p = val;
            (*len)++;
            p++;
        }

        remaining_bytes--;
    }

    return 0;
}