/*
 * Copyright (c) 2026 Silicon Laboratories Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>
#include <psa/crypto.h>

/*
 * EC-JPAKE (secp256r1, SHA-256) between a client and a server, then the TLS
 * premaster secret. PSA_ALG_TLS12_ECJPAKE_TO_PMS is SHA-256 of the x-coordinate
 * of the shared secret.
 *
 * The exchange uses the multipart PAKE API: each round is a sequence of
 * KEY_SHARE, ZK_PUBLIC and ZK_PROOF steps. The premaster secret is then
 * derived twice, and the results must match:
 *  - from the key produced by psa_pake_get_shared_key(), and
 *  - from that key's exported bytes, passed in one shot to the derivation.
 */

static const uint8_t password[] = {
	'p', 'a', 's', 's', 'w', 'o', 'r', 'd',
};
static const uint8_t client_id[] = {
	'c', 'l', 'i', 'e', 'n', 't',
};
static const uint8_t server_id[] = {
	's', 'e', 'r', 'v', 'e', 'r',
};

static const psa_pake_step_t jpake_steps[] = {
	PSA_PAKE_STEP_KEY_SHARE,
	PSA_PAKE_STEP_ZK_PUBLIC,
	PSA_PAKE_STEP_ZK_PROOF,
};

struct jpake_message {
	uint8_t data[PSA_PAKE_OUTPUT_MAX_SIZE];
	size_t length;
};

/* Round 1 carries two Schnorr triples; round 2 carries one. */
#define JPAKE_ROUND1_STEPS 6
#define JPAKE_ROUND2_STEPS 3

static void jpake_output(psa_pake_operation_t *operation, struct jpake_message *messages,
			 unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		zassert_equal(psa_pake_output(operation, jpake_steps[i % ARRAY_SIZE(jpake_steps)],
					      messages[i].data, sizeof(messages[i].data),
					      &messages[i].length),
			      PSA_SUCCESS, "Failed to output PAKE step");
		zassert_true(messages[i].length > 0, "PAKE step produced no data");
	}
}

static void jpake_input(psa_pake_operation_t *operation, const struct jpake_message *messages,
			unsigned int count)
{
	for (unsigned int i = 0; i < count; i++) {
		zassert_equal(psa_pake_input(operation, jpake_steps[i % ARRAY_SIZE(jpake_steps)],
					     messages[i].data, messages[i].length),
			      PSA_SUCCESS, "Failed to input PAKE step");
	}
}

static void derive_tls_pms(psa_key_id_t shared_key, const uint8_t *secret, size_t secret_length,
			   uint8_t pms[PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE])
{
	psa_key_derivation_operation_t operation = PSA_KEY_DERIVATION_OPERATION_INIT;

	zassert_equal(psa_key_derivation_setup(&operation, PSA_ALG_TLS12_ECJPAKE_TO_PMS),
		      PSA_SUCCESS, "Failed to set up ECJPAKE-to-PMS derivation");

	if (shared_key != PSA_KEY_ID_NULL) {
		zassert_equal(psa_key_derivation_input_key(&operation,
							   PSA_KEY_DERIVATION_INPUT_SECRET,
							   shared_key),
			      PSA_SUCCESS, "Failed to input shared key");
	} else {
		zassert_equal(psa_key_derivation_input_bytes(&operation,
							     PSA_KEY_DERIVATION_INPUT_SECRET,
							     secret, secret_length),
			      PSA_SUCCESS, "Failed to input shared secret");
	}

	zassert_equal(psa_key_derivation_output_bytes(&operation, pms,
						      PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE),
		      PSA_SUCCESS, "Failed to read TLS premaster secret");
	zassert_equal(psa_key_derivation_abort(&operation), PSA_SUCCESS,
		      "Failed to abort derivation");
}

/* Operations and round messages stay off the test thread stack. Software
 * EC-JPAKE keeps a full curve context in each operation. */
static psa_pake_operation_t client_op;
static psa_pake_operation_t server_op;
static struct jpake_message client_round1[JPAKE_ROUND1_STEPS];
static struct jpake_message server_round1[JPAKE_ROUND1_STEPS];
static struct jpake_message client_round2[JPAKE_ROUND2_STEPS];
static struct jpake_message server_round2[JPAKE_ROUND2_STEPS];

ZTEST(psa_crypto_test, test_pake_ecjpake_sha256_pms)
{
	psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
	psa_pake_cipher_suite_t cipher_suite = PSA_PAKE_CIPHER_SUITE_INIT;
	psa_pake_operation_t *client = &client_op;
	psa_pake_operation_t *server = &server_op;
	psa_key_id_t password_key = PSA_KEY_ID_NULL;
	psa_key_id_t client_key = PSA_KEY_ID_NULL;
	psa_key_id_t server_key = PSA_KEY_ID_NULL;
	uint8_t client_secret[PSA_PAKE_OUTPUT_MAX_SIZE];
	uint8_t server_secret[PSA_PAKE_OUTPUT_MAX_SIZE];
	size_t client_secret_length = 0;
	size_t server_secret_length = 0;
	uint8_t client_pms_from_key[PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE];
	uint8_t server_pms_from_key[PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE];
	uint8_t client_pms_from_bytes[PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE];
	uint8_t server_pms_from_bytes[PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE];
	const psa_algorithm_t jpake = PSA_ALG_JPAKE(PSA_ALG_SHA_256);

	*client = psa_pake_operation_init();
	*server = psa_pake_operation_init();

	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
	psa_set_key_algorithm(&attributes, jpake);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_PASSWORD);
	zassert_equal(psa_import_key(&attributes, password, sizeof(password), &password_key),
		      PSA_SUCCESS, "Failed to import password");

	psa_pake_cs_set_algorithm(&cipher_suite, jpake);
	psa_pake_cs_set_primitive(&cipher_suite,
				  PSA_PAKE_PRIMITIVE(PSA_PAKE_PRIMITIVE_TYPE_ECC,
						     PSA_ECC_FAMILY_SECP_R1, 256));
	psa_pake_cs_set_key_confirmation(&cipher_suite, PSA_PAKE_UNCONFIRMED_KEY);

	zassert_equal(psa_pake_setup(client, password_key, &cipher_suite), PSA_SUCCESS,
		      "Failed to set up client PAKE");
	zassert_equal(psa_pake_set_user(client, client_id, sizeof(client_id)), PSA_SUCCESS,
		      "Failed to set client user id");
	zassert_equal(psa_pake_set_peer(client, server_id, sizeof(server_id)), PSA_SUCCESS,
		      "Failed to set client peer id");

	zassert_equal(psa_pake_setup(server, password_key, &cipher_suite), PSA_SUCCESS,
		      "Failed to set up server PAKE");
	zassert_equal(psa_pake_set_user(server, server_id, sizeof(server_id)), PSA_SUCCESS,
		      "Failed to set server user id");
	zassert_equal(psa_pake_set_peer(server, client_id, sizeof(client_id)), PSA_SUCCESS,
		      "Failed to set server peer id");

	/* Round 1: each side outputs both Schnorr triples, then consumes the peer's. */
	jpake_output(server, server_round1, JPAKE_ROUND1_STEPS);
	jpake_output(client, client_round1, JPAKE_ROUND1_STEPS);
	jpake_input(client, server_round1, JPAKE_ROUND1_STEPS);
	jpake_input(server, client_round1, JPAKE_ROUND1_STEPS);

	/* Round 2: one Schnorr triple each way. */
	jpake_output(server, server_round2, JPAKE_ROUND2_STEPS);
	jpake_output(client, client_round2, JPAKE_ROUND2_STEPS);
	jpake_input(client, server_round2, JPAKE_ROUND2_STEPS);
	jpake_input(server, client_round2, JPAKE_ROUND2_STEPS);

	attributes = psa_key_attributes_init();
	psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
	psa_set_key_algorithm(&attributes, PSA_ALG_TLS12_ECJPAKE_TO_PMS);
	psa_set_key_type(&attributes, PSA_KEY_TYPE_DERIVE);

	zassert_equal(psa_pake_get_shared_key(client, &attributes, &client_key), PSA_SUCCESS,
		      "Failed to get client shared key");
	zassert_equal(psa_pake_get_shared_key(server, &attributes, &server_key), PSA_SUCCESS,
		      "Failed to get server shared key");

	zassert_equal(psa_export_key(client_key, client_secret, sizeof(client_secret),
				     &client_secret_length),
		      PSA_SUCCESS, "Failed to export client shared secret");
	zassert_equal(psa_export_key(server_key, server_secret, sizeof(server_secret),
				     &server_secret_length),
		      PSA_SUCCESS, "Failed to export server shared secret");
	zassert_true(client_secret_length > 0 && server_secret_length > 0,
		     "Shared secret is empty");

	derive_tls_pms(client_key, NULL, 0, client_pms_from_key);
	derive_tls_pms(server_key, NULL, 0, server_pms_from_key);
	derive_tls_pms(PSA_KEY_ID_NULL, client_secret, client_secret_length, client_pms_from_bytes);
	derive_tls_pms(PSA_KEY_ID_NULL, server_secret, server_secret_length, server_pms_from_bytes);

	zassert_mem_equal(client_pms_from_key, server_pms_from_key,
			  PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE,
			  "Client and server premaster secrets differ");
	zassert_mem_equal(client_pms_from_key, client_pms_from_bytes,
			  PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE,
			  "Key and byte inputs derived different premaster secrets");
	zassert_mem_equal(server_pms_from_key, server_pms_from_bytes,
			  PSA_TLS12_ECJPAKE_TO_PMS_DATA_SIZE,
			  "Key and byte inputs derived different premaster secrets");

	psa_destroy_key(password_key);
	psa_destroy_key(client_key);
	psa_destroy_key(server_key);
	memset(client_secret, 0, sizeof(client_secret));
	memset(server_secret, 0, sizeof(server_secret));
}
