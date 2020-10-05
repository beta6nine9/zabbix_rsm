#ifndef _RSM_H_
#define _RSM_H_

#include <openssl/evp.h>
#include <termios.h>

#define RSM_EPP_SENSDATA_MAX	8192
#define RSM_EPP_PASSPHRASE_MAX	64

int	rsm_ssl_init(void);
int	get_random(void *data, int bytes);
void	zbx_ssl_get_error(char *err, size_t err_size);
int	encrypt(const EVP_CIPHER *cipher, unsigned char *plaintext, int plaintext_len, unsigned char *key,
		unsigned char *iv, unsigned char *ciphertext, int *ciphertext_len, char *err, size_t err_size);
int	encrypt_cleartext(const char *passphrase, size_t passphrase_len, const char *secretkey_enc_b64,
		size_t secretkey_enc_b64_len, const char *secretkey_salt_b64, size_t secretkey_salt_b64_len,
		const char *cleartext, size_t cleartext_len, char **cleartext_b64, char **cleartext_salt_b64,
		char *err, size_t err_size);
int	decrypt_ciphertext(const char *passphrase, size_t passphrase_len, const char *secretkey_enc_b64,
		size_t secretkey_enc_b64_len, const char *secretkey_salt_b64, size_t secretkey_salt_b64_len,
		const char *cleartext_enc_b64, size_t cleartext_enc_b64_len, const char *cleartext_salt_b64,
		size_t cleartext_salt_b64_len, char **cleartext, char *err, size_t err_size);
int	zbx_read_stdin(const char *prompt, char *output, size_t output_size, char *err, size_t err_size);

#endif /* _RSM_H_ */
