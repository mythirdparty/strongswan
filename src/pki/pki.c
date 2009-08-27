/*
 * Copyright (C) 2009 Martin Willi
 * Hochschule fuer Technik Rapperswil
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.  See <http://www.fsf.org/copyleft/gpl.txt>.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 */

#define _GNU_SOURCE
#include <getopt.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include <library.h>
#include <credentials/keys/private_key.h>
#include <credentials/certificates/certificate.h>
#include <credentials/certificates/x509.h>

static int usage(char *error)
{
	FILE *out = stdout;
	
	if (error)
	{
		out = stderr;
		fprintf(out, "Error: %s\n", error);
	}
	fprintf(out, "strongSwan %s PKI tool\n", VERSION);
	fprintf(out, "usage:\n");
	fprintf(out, "  pki --help\n");
	fprintf(out, "      show this usage information\n");
	fprintf(out, "  pki --gen [--type rsa|ecdsa] [--size bits] [--outform der|pem|pgp]\n");
	fprintf(out, "      generate a new private key\n");
	fprintf(out, "        --type     type of key, default: rsa\n");
	fprintf(out, "        --size     keylength in bits, default: rsa 2048, ecdsa 384\n");
	fprintf(out, "        --outform  encoding of generated private key\n");
	fprintf(out, "  pki --pub [--in file] [--type rsa|ecdsa|x509] [--outform der|pem|pgp]\n");
	fprintf(out, "      extract the public key from a private key/certificate\n");
	fprintf(out, "        --in       input file, default: stdin\n");
	fprintf(out, "        --type     type of credential, default: rsa\n");
	fprintf(out, "        --outform  encoding of extracted public key\n");
	fprintf(out, "  pki --keyid [--in file] [--type rsa-priv|ecdsa-priv|pub|x509]\n");
	fprintf(out, "      calculate key identifiers of a key/certificate\n");
	fprintf(out, "        --in       input file, default: stdin\n");
	fprintf(out, "        --type     type of key, default: rsa-priv\n");
	fprintf(out, "  pki --self [--in file] [--type rsa|ecdsa] --dn distinguished-name\n");
	fprintf(out, "             [--lifetime days] [--serial hex]\n");
	fprintf(out, "      create a self signed certificate\n");
	fprintf(out, "        --in       private key input file, default: stdin\n");
	fprintf(out, "        --type     type of input key, default: rsa\n");
	fprintf(out, "        --dn       subject and issuer distinguished name\n");
	fprintf(out, "        --lifetime days the certificate is valid, default: 1080\n");
	fprintf(out, "        --serial   serial number in hex, default: random\n");
	return !!error;
}

/**
 * Convert a form string to a encoding type
 */
static bool get_form(char *form, key_encoding_type_t *type, bool pub)
{
	if (streq(form, "der"))
	{
		/* der encoded keys usually contain the complete SubjectPublicKeyInfo */
		*type = pub ? KEY_PUB_SPKI_ASN1_DER : KEY_PRIV_ASN1_DER;
	}
	else if (streq(form, "pem"))
	{
		*type = pub ? KEY_PUB_PEM : KEY_PRIV_PEM;
	}
	else if (streq(form, "pgp"))
	{
		*type = pub ? KEY_PUB_PGP : KEY_PRIV_PGP;
	}
	else
	{
		return FALSE;
	}
	return TRUE;
}

/**
 * Generate a private key
 */
static int gen(int argc, char *argv[])
{
	key_encoding_type_t form = KEY_PRIV_ASN1_DER;
	key_type_t type = KEY_RSA;
	u_int size = 0;
	private_key_t *key;
	chunk_t encoding;
	
	struct option long_opts[] = {
		{ "type", required_argument, NULL, 't' },
		{ "size", required_argument, NULL, 's' },
		{ "outform", required_argument, NULL, 'o' },
		{ 0,0,0,0 }
	};
	while (TRUE)
	{
		switch (getopt_long(argc, argv, "", long_opts, NULL))
		{
			case 't':
				if (streq(optarg, "rsa"))
				{
					type = KEY_RSA;
				}
				else if (streq(optarg, "ecdsa"))
				{
					type = KEY_ECDSA;
				}
				else
				{
					return usage("invalid key type");
				}
				continue;
			case 'o':
				if (!get_form(optarg, &form, FALSE))
				{
					return usage("invalid key output format");
				}
				continue;
			case 's':
				size = atoi(optarg);
				if (!size)
				{
					return usage("invalid key size");
				}
				continue;
			case EOF:
				break;
			default:
				return usage("invalid --gen option");
		}
		break;
	}
	/* default key sizes */
	if (!size)
	{
		switch (type)
		{
			case KEY_RSA:
				size = 2048;
				break;
			case KEY_ECDSA:
				size = 384;
				break;
			default:
				break;
		}
	}
	key = lib->creds->create(lib->creds, CRED_PRIVATE_KEY, type,
							 BUILD_KEY_SIZE, size, BUILD_END);
	if (!key)
	{
		fprintf(stderr, "private key generation failed\n");
		return 1;
	}
	if (!key->get_encoding(key, form, &encoding))
	{
		fprintf(stderr, "private key encoding failed\n");
		key->destroy(key);
		return 1;
	}
	key->destroy(key);
	if (fwrite(encoding.ptr, encoding.len, 1, stdout) != 1)
	{
		fprintf(stderr, "writing private key failed\n");
		free(encoding.ptr);
		return 1;
	}
	free(encoding.ptr);
	return 0;
}

/**
 * Extract a public key from a private key/certificate
 */
static int pub(int argc, char *argv[])
{
	key_encoding_type_t form = KEY_PUB_SPKI_ASN1_DER;
	credential_type_t type = CRED_PRIVATE_KEY;
	int subtype = KEY_RSA;
	certificate_t *cert;
	private_key_t *private;
	public_key_t *public;
	chunk_t encoding;
	char *file = NULL;
	void *cred;
	
	struct option long_opts[] = {
		{ "type", required_argument, NULL, 't' },
		{ "outform", required_argument, NULL, 'f' },
		{ "in", required_argument, NULL, 'i' },
		{ 0,0,0,0 }
	};
	while (TRUE)
	{
		switch (getopt_long(argc, argv, "", long_opts, NULL))
		{
			case 't':
				if (streq(optarg, "rsa"))
				{
					type = CRED_PRIVATE_KEY;
					subtype = KEY_RSA;
				}
				else if (streq(optarg, "ecdsa"))
				{
					type = CRED_PRIVATE_KEY;
					subtype = KEY_ECDSA;
				}
				else if (streq(optarg, "x509"))
				{
					type = CRED_CERTIFICATE;
					subtype = CERT_X509;
				}
				else
				{
					return usage("invalid input type");
				}
				continue;
			case 'f':
				if (!get_form(optarg, &form, TRUE))
				{
					return usage("invalid output format");
				}
				continue;
			case 'i':
				file = optarg;
				continue;
			case EOF:
				break;
			default:
				return usage("invalid --pub option");
		}
		break;
	}
	if (file)
	{
		cred = lib->creds->create(lib->creds, type, subtype,
									 BUILD_FROM_FILE, file, BUILD_END);
	}
	else
	{
		cred = lib->creds->create(lib->creds, type, subtype,
									 BUILD_FROM_FD, 0, BUILD_END);
	}
	
	if (type == CRED_PRIVATE_KEY)
	{
		private = cred;
		if (!private)
		{
			fprintf(stderr, "parsing private key failed\n");
			return 1;
		}
		public = private->get_public_key(private);
		private->destroy(private);
	}
	else
	{
		cert = cred;
		if (!cert)
		{
			fprintf(stderr, "parsing certificate failed\n");
			return 1;
		}
		public = cert->get_public_key(cert);
		cert->destroy(cert);
	}
	if (!public)
	{
		fprintf(stderr, "extracting public key failed\n");
		return 1;
	}
	if (!public->get_encoding(public, form, &encoding))
	{
		fprintf(stderr, "public key encoding failed\n");
		public->destroy(public);
		return 1;
	}
	public->destroy(public);
	if (fwrite(encoding.ptr, encoding.len, 1, stdout) != 1)
	{
		fprintf(stderr, "writing public key failed\n");
		free(encoding.ptr);
		return 1;
	}
	free(encoding.ptr);
	return 0;
}

/**
 * Calculate the keyid of a key/certificate
 */
static int keyid(int argc, char *argv[])
{
	credential_type_t type = CRED_PRIVATE_KEY;
	int subtype = KEY_RSA;
	certificate_t *cert;
	private_key_t *private;
	public_key_t *public;
	char *file = NULL;
	void *cred;
	chunk_t id;
	
	struct option long_opts[] = {
		{ "type", required_argument, NULL, 't' },
		{ "in", required_argument, NULL, 'i' },
		{ 0,0,0,0 }
	};
	while (TRUE)
	{
		switch (getopt_long(argc, argv, "", long_opts, NULL))
		{
			case 't':
				if (streq(optarg, "rsa-priv"))
				{
					type = CRED_PRIVATE_KEY;
					subtype = KEY_RSA;
				}
				else if (streq(optarg, "ecdsa-priv"))
				{
					type = CRED_PRIVATE_KEY;
					subtype = KEY_ECDSA;
				}
				else if (streq(optarg, "pub"))
				{
					type = CRED_PUBLIC_KEY;
					subtype = KEY_ANY;
				}
				else if (streq(optarg, "x509"))
				{
					type = CRED_CERTIFICATE;
					subtype = CERT_X509;
				}
				else
				{
					return usage("invalid input type");
				}
				continue;
			case 'i':
				file = optarg;
				continue;
			case EOF:
				break;
			default:
				return usage("invalid --keyid option");
		}
		break;
	}
	if (file)
	{
		cred = lib->creds->create(lib->creds, type, subtype,
								  BUILD_FROM_FILE, file, BUILD_END);
	}
	else
	{
		cred = lib->creds->create(lib->creds, type, subtype,
								  BUILD_FROM_FD, 0, BUILD_END);
	}
	if (!cred)
	{
		fprintf(stderr, "parsing input failed\n");
		return 1;
	}
	
	if (type == CRED_PRIVATE_KEY)
	{
		private = cred;
		if (private->get_fingerprint(private, KEY_ID_PUBKEY_SHA1, &id))
		{
			printf("subject key identifier:    %#B\n", &id);
		}
		if (private->get_fingerprint(private, KEY_ID_PUBKEY_INFO_SHA1, &id))
		{
			printf("subjectPublicKeyInfo hash: %#B\n", &id);
		}
		private->destroy(private);
	}
	else if (type == CRED_PUBLIC_KEY)
	{
		public = cred;
		if (public->get_fingerprint(public, KEY_ID_PUBKEY_SHA1, &id))
		{
			printf("subject key identifier:    %#B\n", &id);
		}
		if (public->get_fingerprint(public, KEY_ID_PUBKEY_INFO_SHA1, &id))
		{
			printf("subjectPublicKeyInfo hash: %#B\n", &id);
		}
		public->destroy(public);
	}
	else
	{
		cert = cred;
		public = cert->get_public_key(cert);
		if (!public)
		{
			fprintf(stderr, "extracting public key from certificate failed");
			return 1;
		}
		if (public->get_fingerprint(public, KEY_ID_PUBKEY_SHA1, &id))
		{
			printf("subject key identifier:    %#B\n", &id);
		}
		if (public->get_fingerprint(public, KEY_ID_PUBKEY_INFO_SHA1, &id))
		{
			printf("subjectPublicKeyInfo hash: %#B\n", &id);
		}
		public->destroy(public);
		cert->destroy(cert);
	}
	return 0;
}

/**
 * Create a self signed certificate.
 */
static int self(int argc, char *argv[])
{
	key_type_t type = KEY_RSA;
	certificate_t *cert;
	private_key_t *private;
	public_key_t *public;
	char *file = NULL, *dn = NULL, *hex = NULL;
	identification_t *id;
	int lifetime = 1080;
	chunk_t serial, encoding;
	time_t not_before, not_after;
	
	struct option long_opts[] = {
		{ "type", required_argument, NULL, 't' },
		{ "in", required_argument, NULL, 'i' },
		{ "dn", required_argument, NULL, 'd' },
		{ "lifetime", required_argument, NULL, 'l' },
		{ "serial", required_argument, NULL, 's' },
		{ 0,0,0,0 }
	};
	
	while (TRUE)
	{
		switch (getopt_long(argc, argv, "", long_opts, NULL))
		{
			case 't':
				if (streq(optarg, "rsa"))
				{
					type = KEY_RSA;
				}
				else if (streq(optarg, "ecdsa"))
				{
					type = KEY_ECDSA;
				}
				else
				{
					return usage("invalid input type");
				}
				continue;
			case 'i':
				file = optarg;
				continue;
			case 'd':
				dn = optarg;
				continue;
			case 'l':
				lifetime = atoi(optarg);
				if (!lifetime)
				{
					return usage("invalid --lifetime value");
				}
				continue;
			case 's':
				hex = optarg;
				continue;
			case EOF:
				break;
			default:
				return usage("invalid --self option");
		}
		break;
	}
	
	if (!dn)
	{
		return usage("--dn is required");
	}
	id = identification_create_from_string(dn);
	if (id->get_type(id) != ID_DER_ASN1_DN)
	{
		id->destroy(id);
		fprintf(stderr, "supplied --dn is not a distinguished name\n");
		return 1;
	}
	if (file)
	{
		private = lib->creds->create(lib->creds, CRED_PRIVATE_KEY, type,
									 BUILD_FROM_FILE, file, BUILD_END);
	}
	else
	{
		private = lib->creds->create(lib->creds, CRED_PRIVATE_KEY, type,
									 BUILD_FROM_FD, 0, BUILD_END);
	}
	if (!private)
	{
		id->destroy(id);
		fprintf(stderr, "parsing private key failed\n");
		return 1;
	}
	public = private->get_public_key(private);
	if (!public)
	{
		private->destroy(private);
		id->destroy(id);
		fprintf(stderr, "extracting public key failed\n");
		return 1;
	}
	if (hex)
	{
		serial = chunk_from_hex(chunk_create(hex, strlen(hex)), NULL);
	}
	else
	{
		rng_t *rng = lib->crypto->create_rng(lib->crypto, RNG_WEAK);
		if (!rng)
		{
			id->destroy(id);
			private->destroy(private);
			public->destroy(public);
			fprintf(stderr, "no random number generator found\n");
			return 1;
		}
		rng->allocate_bytes(rng, 8, &serial);
		rng->destroy(rng);
	}
	not_before = time(NULL);
	not_after = not_before + lifetime * 24 * 60 * 60;
	cert = lib->creds->create(lib->creds, CRED_CERTIFICATE, CERT_X509,
						BUILD_SIGNING_KEY, private, BUILD_PUBLIC_KEY, public,
						BUILD_SUBJECT, id, BUILD_NOT_BEFORE_TIME, not_before,
						BUILD_NOT_AFTER_TIME, not_after, BUILD_SERIAL, serial,
						BUILD_END);
	private->destroy(private);
	public->destroy(public);
	id->destroy(id);
	free(serial.ptr);
	if (!cert)
	{
		fprintf(stderr, "generating certificate failed\n");
		return 1;
	}
	encoding = cert->get_encoding(cert);
	if (!encoding.ptr)
	{
		cert->destroy(cert);
		fprintf(stderr, "encoding certificate failed\n");
		return 1;
	}
	cert->destroy(cert);
	if (fwrite(encoding.ptr, encoding.len, 1, stdout) != 1)
	{
		fprintf(stderr, "writing certificate key failed\n");
		free(encoding.ptr);
		return 1;
	}
	free(encoding.ptr);
	return 0;
}

/**
 * Library initialization and operation parsing
 */
int main(int argc, char *argv[])
{
	struct option long_opts[] = {
		{ "help", no_argument, NULL, 'h' },
		{ "gen", no_argument, NULL, 'g' },
		{ "pub", no_argument, NULL, 'p' },
		{ "keyid", no_argument, NULL, 'k' },
		{ "self", no_argument, NULL, 's' },
		{ 0,0,0,0 }
	};
	
	atexit(library_deinit);
	if (!library_init(STRONGSWAN_CONF))
	{
		exit(SS_RC_LIBSTRONGSWAN_INTEGRITY);
	}
	if (lib->integrity &&
		!lib->integrity->check_file(lib->integrity, "pki", argv[0]))
	{
		fprintf(stderr, "integrity check of pki failed\n");
		exit(SS_RC_DAEMON_INTEGRITY);
	}
	lib->plugins->load(lib->plugins, PLUGINDIR,
		lib->settings->get_str(lib->settings, "pki.load", PLUGINS));
	
	switch (getopt_long(argc, argv, "", long_opts, NULL))
	{
		case 'h':
			return usage(NULL);
		case 'g':
			return gen(argc, argv);
		case 'p':
			return pub(argc, argv);
		case 'k':
			return keyid(argc, argv);
		case 's':
			return self(argc, argv);
		default:
			return usage("invalid operation");
	}
}

