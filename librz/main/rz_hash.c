// SPDX-License-Identifier: LGPL-3.0-only

#include <stdio.h>
#include <string.h>
#include <rz_io.h>
#include <rz_main.h>
#include <rz_hash.h>
#include <rz_util/rz_print.h>
#include <rz_util.h>
#include <rz_crypto.h>

static ut64 from = 0LL;
static ut64 to = 0LL;
static bool incremental = true;
static int iterations = 0;
static int quiet = 0;
static RzHashSeed s = {
	0
},
		  *_s = NULL;

static void compare_hashes(const RzHash *ctx, const ut8 *compare, int length, int *ret) {
	if (compare) {
		// algobit has only 1 bit set
		if (!memcmp(ctx->digest, compare, length)) {
			printf("rz-hash: Computed hash matches the expected one.\n");
		} else {
			eprintf("rz-hash: Computed hash doesn't match the expected one.\n");
			*ret = 1;
		}
	}
}

static void do_hash_seed(const char *seed) {
	const char *sptr = seed;
	if (!seed) {
		_s = NULL;
		return;
	}
	_s = &s;
	if (!strcmp(seed, "-")) {
		s.buf = (ut8 *)rz_stdin_slurp(&s.len);
		return;
	}
	if (seed[0] == '@') {
		size_t len;
		s.buf = (ut8 *)rz_file_slurp(seed + 1, &len);
		s.len = (size_t)len;
		return;
	}
	s.buf = (ut8 *)malloc(strlen(seed) + 128);
	if (!s.buf) {
		_s = NULL;
		return;
	}
	if (*seed == '^') {
		s.prefix = 1;
		sptr++;
	} else {
		s.prefix = 0;
	}
	if (!strncmp(sptr, "s:", 2)) {
		strcpy((char *)s.buf, sptr + 2);
		s.len = strlen(sptr + 2);
	} else {
		s.len = rz_hex_str2bin(sptr, s.buf);
		if (s.len < 1) {
			strcpy((char *)s.buf, sptr);
			s.len = strlen(sptr);
			eprintf("Warning: This is not an hexpair, assuming a string, prefix it with 's:' to skip this message.");
		}
	}
}

static void do_hash_hexprint(const ut8 *c, int len, int ule, int rad) {
	int i;
	if (ule) {
		for (i = len - 1; i >= 0; i--) {
			printf("%02x", c[i]);
		}
	} else {
		for (i = 0; i < len; i++) {
			printf("%02x", c[i]);
		}
	}
	if (rad != 'j') {
		printf("\n");
	}
}

static void do_hash_print(RzHash *ctx, ut64 hash, int dlen, int rad, int ule) {
	char *o;
	const ut8 *c = ctx->digest;
	const char *hname = rz_hash_name(hash);
	switch (rad) {
	case 0:
		if (!quiet) {
			printf("0x%08" PFMT64x "-0x%08" PFMT64x " %s: ",
				from, to > 0 ? to - 1 : 0, hname);
		}
		if (dlen == RZ_HASH_SIZE_ENTROPY) {
			printf("%.8f\n", ctx->entropy);
		} else {
			do_hash_hexprint(c, dlen, ule, rad);
		}
		break;
	case 'n':
		do_hash_hexprint(c, dlen, ule, 'j');
		break;
	case 'j':
		printf("{\"name\":\"%s\",\"hash\":\"", hname);
		do_hash_hexprint(c, dlen, ule, rad);
		printf("\"}");
		break;
	default:
		o = rz_print_randomart(c, dlen, from);
		printf("%s\n%s\n", hname, o);
		free(o);
		break;
	}
}

static int do_hash_internal(RzHash *ctx, ut64 hash, const ut8 *buf, int len, int rad, int print, int le) {
	if (len < 0) {
		return 0;
	}
	int dlen = rz_hash_calculate(ctx, hash, buf, len);
	if (!print) {
		return 1;
	}
	if (iterations > 0) {
		rz_hash_do_spice(ctx, hash, iterations, _s);
	}
	do_hash_print(ctx, hash, dlen, rad, le);
	return 1;
}

static int do_hash(const char *file, const char *algo, RzIO *io, int bsize, int rad, int ule, const ut8 *compare) {
	ut64 j, fsize, algobit = rz_hash_name_to_bits(algo);
	RzHash *ctx;
	ut8 *buf;
	int ret = 0;
	ut64 i;
	bool first = true;
	if (algobit == RZ_HASH_NONE) {
		eprintf("rz-hash: Invalid hashing algorithm specified\n");
		return 1;
	}
	fsize = rz_io_desc_size(io->desc);
	if (fsize < 1) {
		eprintf("rz-hash: Invalid file size\n");
		return 1;
	}
	if (bsize < 0) {
		bsize = fsize / -bsize;
	}
	if (bsize == 0 || bsize > fsize) {
		bsize = fsize;
	}
	if (to == 0LL) {
		to = fsize;
	}
	if (from > to) {
		eprintf("rz-hash: Invalid -f -t range\n");
		return 1;
	}
	if (fsize == -1LL) {
		eprintf("rz-hash: Unknown file size\n");
		return 1;
	}
	buf = calloc(1, bsize + 1);
	if (!buf) {
		return 1;
	}
	ctx = rz_hash_new(true, algobit);

	if (rad == 'j') {
		printf("[");
	}
	if (incremental) {
		for (i = 1; i < RZ_HASH_ALL; i <<= 1) {
			if (algobit & i) {
				ut64 hashbit = i & algobit;
				int dlen = rz_hash_size(hashbit);
				rz_hash_do_begin(ctx, i);
				if (s.buf && s.prefix) {
					do_hash_internal(ctx, hashbit, s.buf, s.len, rad, 0, ule);
				}
				for (j = from; j < to; j += bsize) {
					int len = ((j + bsize) > to) ? (to - j) : bsize;
					rz_io_pread_at(io, j, buf, len);
					do_hash_internal(ctx, hashbit, buf, len, rad, 0, ule);
				}
				if (s.buf && !s.prefix) {
					do_hash_internal(ctx, hashbit, s.buf, s.len, rad, 0, ule);
				}
				rz_hash_do_end(ctx, i);
				if (iterations > 0) {
					rz_hash_do_spice(ctx, i, iterations, _s);
				}
				if (!*rz_hash_name(i)) {
					continue;
				}
				if (rad == 'j') {
					if (first) {
						first = false;
					} else {
						printf(",");
					}
				}
				if (!quiet && rad != 'j') {
					printf("%s: ", file);
				}
				do_hash_print(ctx, i, dlen, quiet ? 'n' : rad, ule);
				if (quiet == 1) {
					printf(" %s\n", file);
				} else {
					if (quiet && !rad) {
						printf("\n");
					}
				}
			}
		}
		if (_s) {
			free(_s->buf);
		}
	} else {
		/* iterate over all algorithm bits */
		if (s.buf) {
			eprintf("Warning: Seed ignored on per-block hashing.\n");
		}
		for (i = 1; i < RZ_HASH_ALL; i <<= 1) {
			ut64 f, t, ofrom, oto;
			if (algobit & i) {
				ut64 hashbit = i & algobit;
				ofrom = from;
				oto = to;
				f = from;
				t = to;
				for (j = f; j < t; j += bsize) {
					int nsize = (j + bsize < fsize) ? bsize : (fsize - j);
					rz_io_pread_at(io, j, buf, bsize);
					from = j;
					to = j + bsize;
					if (to > fsize) {
						to = fsize;
					}
					do_hash_internal(ctx, hashbit, buf, nsize, rad, 1, ule);
				}
				do_hash_internal(ctx, hashbit, NULL, 0, rad, 1, ule);
				from = ofrom;
				to = oto;
			}
		}
	}
	if (rad == 'j') {
		printf("]\n");
	}

	compare_hashes(ctx, compare, rz_hash_size(algobit), &ret);
	rz_hash_free(ctx);
	free(buf);
	return ret;
}

static int do_help(int line) {
	printf("Usage: rz-hash [-rBhLkv] [-b S] [-a A] [-c H] [-E A] [-s S] [-f O] [-t O] [file] ...\n");
	if (line) {
		return 0;
	}
	printf(
		" -a algo     comma separated list of algorithms (default is 'sha256')\n"
		" -b bsize    specify the size of the block (instead of full file)\n"
		" -B          show per-block hash\n"
		" -c hash     compare with this hash\n"
		" -e          swap endian (use little endian)\n"
		" -E algo     encrypt. Use -S to set key and -I to set IV\n"
		" -D algo     decrypt. Use -S to set key and -I to set IV\n"
		" -f from     start hashing at given address\n"
		" -i num      repeat hash N iterations\n"
		" -I iv       use give initialization vector (IV) (hexa or s:string)\n"
		" -S seed     use given seed (hexa or s:string) use ^ to prefix (key for -E)\n"
		"             (- will slurp the key from stdin, the @ prefix points to a file\n"
		" -k          show hash using the openssh's randomkey algorithm\n"
		" -q          run in quiet mode (-qq to show only the hash)\n"
		" -L          list all available algorithms (see -a)\n"
		" -r          output rizin commands\n"
		" -s string   hash this string instead of files\n"
		" -t to       stop hashing at given address\n"
		" -x hexstr   hash this hexpair string instead of files\n"
		" -v          show version information\n");
	return 0;
}

static void algolist(void) {
	ut64 bits;
	ut64 i;
	for (i = 0; i < RZ_HASH_NBITS; i++) {
		bits = 1ULL << i;
		const char *name = rz_hash_name(bits);
		if (name && *name) {
			printf("h  %s\n", name);
		}
	}
	// TODO: do not hardcode
	printf("e  base64\n");
	printf("e  base91\n");
	printf("e  punycode\n");
	for (i = 0;; i++) {
		bits = ((ut64)1) << i;
		const char *name = rz_crypto_name(bits);
		if (!name || !*name) {
			break;
		}
		printf("c  %s\n", name);
	}
}

#define setHashString(x, y) \
	{ \
		if (hashstr) { \
			eprintf("Hashstring already defined\n"); \
			return 1; \
		} \
		hashstr_hex = y; \
		hashstr = (char *)x; \
	}

static bool is_power_of_two(const ut64 x) {
	return x && !(x & (x - 1));
}

// direction: 0 => encrypt, 1 => decrypt
static int encrypt_or_decrypt(const char *algo, int direction, const char *hashstr, int hashstr_len, const ut8 *iv, int ivlen, int mode) {
	bool no_key_mode = !strcmp("base64", algo) || !strcmp("base91", algo) || !strcmp("punycode", algo); // TODO: generalise this for all non key encoding/decoding.
	if (no_key_mode || s.len > 0) {
		RzCrypto *cry = rz_crypto_new();
		if (rz_crypto_use(cry, algo)) {
			if (rz_crypto_set_key(cry, s.buf, s.len, 0, direction)) {
				const char *buf = hashstr;
				int buflen = hashstr_len;

				if (iv && !rz_crypto_set_iv(cry, iv, ivlen)) {
					eprintf("Invalid IV.\n");
					return 0;
				}

				rz_crypto_update(cry, (const ut8 *)buf, buflen);
				rz_crypto_final(cry, NULL, 0);

				int result_size = 0;
				ut8 *result = rz_crypto_get_output(cry, &result_size);
				if (result) {
					if (write(1, result, result_size) != result_size) {
						eprintf("Warning: cannot write result\n");
					}
					free(result);
				}
			} else {
				eprintf("Invalid key\n");
			}
			return 0;
		} else {
			eprintf("Unknown %s algorithm '%s'\n", ((!direction) ? "encryption" : "decryption"), algo);
		}
		rz_crypto_free(cry);
	} else {
		eprintf("%s key not defined. Use -S [key]\n", ((!direction) ? "Encryption" : "Decryption"));
	}
	return 1;
}

static int encrypt_or_decrypt_file(const char *algo, int direction, const char *filename, const ut8 *iv, int ivlen, int mode) {
	bool no_key_mode = !strcmp("base64", algo) || !strcmp("base91", algo) || !strcmp("punycode", algo); // TODO: generalise this for all non key encoding/decoding.
	if (no_key_mode || s.len > 0) {
		RzCrypto *cry = rz_crypto_new();
		if (rz_crypto_use(cry, algo)) {
			if (rz_crypto_set_key(cry, s.buf, s.len, 0, direction)) {
				size_t file_size;
				ut8 *buf;
				if (strcmp(filename, "-") == 0) {
					int sz;
					buf = (ut8 *)rz_stdin_slurp(&sz);
					file_size = (size_t)sz;
				} else {
					buf = (ut8 *)rz_file_slurp(filename, &file_size);
				}
				if (!buf) {
					eprintf("rz-hash: Cannot open '%s'\n", filename);
					return -1;
				}

				if (iv && !rz_crypto_set_iv(cry, iv, ivlen)) {
					eprintf("Invalid IV.\n");
					free(buf);
					return 0;
				}

				rz_crypto_update(cry, buf, file_size);
				rz_crypto_final(cry, NULL, 0);

				int result_size = 0;
				ut8 *result = rz_crypto_get_output(cry, &result_size);
				if (result) {
					(void)write(1, result, result_size);
					free(result);
				}
				free(buf);
			} else {
				eprintf("Invalid key\n");
			}
			return 0;
		} else {
			eprintf("Unknown %s algorithm '%s'\n", ((!direction) ? "encryption" : "decryption"), algo);
		}
		rz_crypto_free(cry);
	} else {
		eprintf("%s key not defined. Use -S [key]\n", ((!direction) ? "Encryption" : "Decryption"));
	}
	return 1;
}

RZ_API int rz_main_rz_hash(int argc, const char **argv) {
	ut64 i;
	int ret, c, rad = 0, bsize = 0, numblocks = 0, ule = 0;
	const char *file = NULL;
	const char *algo = "sha256"; /* default hashing algorithm */
	const char *seed = NULL;
	const char *decrypt = NULL;
	const char *encrypt = NULL;
	char *hashstr = NULL;
	ut8 *iv = NULL;
	int ivlen = -1;
	const char *ivseed = NULL;
	const char *compareStr = NULL;
	const char *ptype = NULL;
	ut8 *compareBin = NULL;
	int hashstr_len = -1;
	int hashstr_hex = 0;
	size_t bytes_read = 0; // bytes read from stdin
	ut64 algobit;
	RzHash *ctx;
	RzIO *io;

	RzGetopt opt;
	rz_getopt_init(&opt, argc, argv, "p:jD:veE:a:i:I:S:s:x:b:nBhf:t:kLqc:");
	while ((c = rz_getopt_next(&opt)) != -1) {
		switch (c) {
		case 'q': quiet++; break;
		case 'i':
			iterations = atoi(opt.arg);
			if (iterations < 0) {
				eprintf("error: -i argument must be positive\n");
				return 1;
			}
			break;
		case 'j': rad = 'j'; break;
		case 'S': seed = opt.arg; break;
		case 'I': ivseed = opt.arg; break;
		case 'n': numblocks = 1; break;
		case 'D': decrypt = opt.arg; break;
		case 'E': encrypt = opt.arg; break;
		case 'L': algolist(); return 0;
		case 'e': ule = 1; break;
		case 'k': rad = 2; break;
		case 'p': ptype = opt.arg; break;
		case 'a': algo = opt.arg; break;
		case 'B': incremental = false; break;
		case 'b': bsize = (int)rz_num_math(NULL, opt.arg); break;
		case 'f': from = rz_num_math(NULL, opt.arg); break;
		case 't': to = 1 + rz_num_math(NULL, opt.arg); break;
		case 'v': return rz_main_version_print("rz_hash");
		case 'h': return do_help(0);
		case 's': setHashString(opt.arg, 0); break;
		case 'x': setHashString(opt.arg, 1); break;
		case 'c': compareStr = opt.arg; break;
		default: return do_help(0);
		}
	}
	if (encrypt && decrypt) {
		eprintf("rz-hash: Option -E and -D are incompatible with each other.\n");
		return 1;
	}
	if (compareStr) {
		int compareBin_len;
		if (bsize && !incremental) {
			eprintf("rz-hash: Option -c incompatible with -b and -B options.\n");
			return 1;
		}
		bool flag = false;
		if (encrypt) {
			flag = !strcmp(encrypt, "base64") || !strcmp(encrypt, "base91");
		} else if (decrypt) {
			flag = !strcmp(decrypt, "base64") || !strcmp(decrypt, "base91");
		}
		if (flag) {
			eprintf("rz-hash: Option -c incompatible with -E base64, -E base91, -D base64 or -D base91 options.\n");
			return 1;
		}
		algobit = rz_hash_name_to_bits(algo);
		// if algobit represents a single algorithm then it's a power of 2
		if (!is_power_of_two(algobit)) {
			eprintf("rz-hash: Option -c incompatible with multiple algorithms in -a.\n");
			return 1;
		}
		compareBin = malloc((strlen(compareStr) + 1) * 2);
		if (!compareBin) {
			return 1;
		}
		compareBin_len = rz_hex_str2bin(compareStr, compareBin);
		if (compareBin_len < 1) {
			eprintf("rz-hash: Invalid -c hex hash\n");
			free(compareBin);
			return 1;
		} else if (compareBin_len != rz_hash_size(algobit)) {
			eprintf(
				"rz-hash: Given -c hash has %d byte(s) but the selected algorithm returns %d byte(s).\n",
				compareBin_len,
				rz_hash_size(algobit));
			free(compareBin);
			return 1;
		}
	}
	if ((st64)from >= 0 && (st64)to < 0) {
		to = 0; // end of file
	}
	if (from || to) {
		if (to && from >= to) {
			eprintf("Invalid -f or -t offsets\n");
			return 1;
		}
	}
	if (ptype) {
		// TODO: support p=%s (horizontal bars)
		// TODO: list supported statistical metrics
		// TODO: support -f and -t
		for (i = opt.ind; i < argc; i++) {
			printf("%s:\n", argv[i]);
			rz_sys_cmdf("rizin -qfnc \"p==%s 100\" \"%s\"", ptype, argv[i]);
		}
		return 0;
	}
	// convert iv to hex or string.
	if (ivseed) {
		iv = (ut8 *)malloc(strlen(ivseed) + 128);
		if (!strncmp(ivseed, "s:", 2)) {
			strcpy((char *)iv, ivseed + 2);
			ivlen = strlen(ivseed + 2);
		} else {
			ivlen = rz_hex_str2bin(ivseed, iv);
			if (ivlen < 1) {
				strcpy((char *)iv, ivseed);
				ivlen = strlen(ivseed);
			}
		}
	}
	do_hash_seed(seed);
	if (hashstr) {
#define INSIZE 32768
		ret = 0;
		if (!strcmp(hashstr, "-")) {
			hashstr = malloc(INSIZE);
			if (!hashstr) {
				free(iv);
				return 1;
			}
			bytes_read = fread((void *)hashstr, 1, INSIZE - 1, stdin);
			if (bytes_read < 1) {
				bytes_read = 0;
			}
			hashstr[bytes_read] = '\0';
			hashstr_len = bytes_read;
		}
		if (hashstr_hex) {
			ut8 *out = malloc((strlen(hashstr) + 1) * 2);
			hashstr_len = rz_hex_str2bin(hashstr, out);
			if (hashstr_len < 1) {
				eprintf("Invalid hex string\n");
				free(out);
				free(iv);
				return 1;
			}
			hashstr = (char *)out;
			/* out memleaks here, hashstr can't be freed */
		} else {
			if (!bytes_read) {
				hashstr_len = strlen(hashstr);
			}
		}
		if (from) {
			if (from >= hashstr_len) {
				eprintf("Invalid -f.\n");
				free(iv);
				return 1;
			}
		}
		if (to) {
			if (to > hashstr_len) {
				eprintf("Invalid -t.\n");
				return 1;
			}
		} else {
			to = hashstr_len;
		}
		hashstr = hashstr + from;
		hashstr_len = to - from;
		hashstr[hashstr_len] = '\0';
		if (!bytes_read && !hashstr_hex) {
			hashstr_len = rz_str_unescape(hashstr);
		}
		if (encrypt) {
			return encrypt_or_decrypt(encrypt, 0, hashstr, hashstr_len, iv, ivlen, 0);
		} else if (decrypt) {
			return encrypt_or_decrypt(decrypt, 1, hashstr, hashstr_len, iv, ivlen, 0);
		} else {
			char *str = (char *)hashstr;
			int strsz = hashstr_len;
			if (_s) {
				// alloc/concat/resize
				str = malloc(strsz + s.len);
				if (s.prefix) {
					memcpy(str, s.buf, s.len);
					memcpy(str + s.len, hashstr, hashstr_len);
				} else {
					memcpy(str, hashstr, hashstr_len);
					memcpy(str + strsz, s.buf, s.len);
				}
				strsz += s.len;
				str[strsz] = 0;
			}
			algobit = rz_hash_name_to_bits(algo);
			if (algobit == 0) {
				eprintf("Invalid algorithm. See -E, -D maybe?\n");
				if (str != hashstr) {
					free(str);
				}
				free(iv);
				return 1;
			}
			for (i = 1; i < RZ_HASH_ALL; i <<= 1) {
				if (algobit & i) {
					ut64 hashbit = i & algobit;
					ctx = rz_hash_new(true, hashbit);
					from = 0;
					to = strsz;
					do_hash_internal(ctx, hashbit, (const ut8 *)str, strsz, rad, 1, ule);
					compare_hashes(ctx, compareBin, rz_hash_size(algobit), &ret);
					rz_hash_free(ctx);
				}
			}
			if (_s) {
				if (str != hashstr) {
					free(str);
				}
				free(s.buf);
			}
			return ret;
		}
	}
	if (opt.ind >= argc) {
		free(iv);
		return do_help(1);
	}
	if (numblocks) {
		bsize = -bsize;
	} else if (bsize < 0) {
		eprintf("rz-hash: Invalid block size\n");
		free(iv);
		return 1;
	}

	io = rz_io_new();
	for (ret = 0, i = opt.ind; i < argc; i++) {
		file = argv[i];

		if (file && !*file) {
			eprintf("Cannot open empty path\n");
			return 1;
		}

		if (encrypt) { // for encrytion when files are provided
			int rt = encrypt_or_decrypt_file(encrypt, 0, argv[i], iv, ivlen, 0);
			if (rt == -1) {
				continue;
			} else {
				return rt;
			}
		} else if (decrypt) {
			int rt = encrypt_or_decrypt_file(decrypt, 1, argv[i], iv, ivlen, 0);
			if (rt == -1) {
				continue;
			} else {
				return rt;
			}
		} else {
			RzIODesc *desc = NULL;
			if (!strcmp(argv[i], "-")) {
				int sz = 0;
				ut8 *buf = (ut8 *)rz_stdin_slurp(&sz);
				char *uri = rz_str_newf("malloc://%d", sz);
				if (sz > 0) {
					desc = rz_io_open_nomap(io, uri, RZ_PERM_R, 0);
					if (!desc) {
						eprintf("rz-hash: Cannot open malloc://1024\n");
						free(iv);
						return 1;
					}
					rz_io_pwrite_at(io, 0, buf, sz);
				}
				free(uri);
			} else {
				if (rz_file_is_directory(argv[i])) {
					eprintf("rz-hash: Cannot hash directories\n");
					free(iv);
					return 1;
				}
				desc = rz_io_open_nomap(io, argv[i], RZ_PERM_R, 0);
				if (!desc) {
					eprintf("rz-hash: Cannot open '%s'\n", argv[i]);
					free(iv);
					return 1;
				}
			}
			ret |= do_hash(argv[i], algo, io, bsize, rad, ule, compareBin);
			to = 0;
			rz_io_desc_close(desc);
		}
	}
	free(hashstr);
	rz_io_free(io);
	free(iv);

	return ret;
}
