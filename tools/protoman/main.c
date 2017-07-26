/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>
#include <string.h>
#include <dirent.h>
#include <errno.h>
#include <inttypes.h>

#include <proto.h>
#include <proto/cache.h>

#include <log.h>
#include <lexer.h>
#include <compiler.h>
#include <sandbox.h>

typedef struct {
	enum {
		CMD_MASK        = 0x7f,
		TARGET          = 0x80,
		CMD_NOP         = 0,
		CMD_INSTALL     = 1 | TARGET,
		CMD_UPDATE      = 2 | TARGET,
		CMD_REMOVE      = 3 | TARGET,
		CMD_LIST_TYPES  = 4,
		CMD_SHOW_INFO   = 5 | TARGET,
		CMD_HELP        = 6,
		CMD_VERSION     = 7,
		CMD_SYNTAX      = 8 | TARGET
	} command;
	int         force;
	int         dry_run;
	int         default_yes;
	int         show_base_type;
	const char* db_root;
	char const* const* target;
	uint32_t    target_count;
	uint32_t    padding_size;
} program_option_t;

#define _PRINT_INFO(message, args...) printf(message"\n", ##args)
#define _PRINT_STDERR(message, args...) printf(message"\n", ##args)
void display_help()
{
	_PRINT_STDERR("protoman: The Plumber centralized protocol type system manangement utilities");
	_PRINT_STDERR("Usage: protoman <command> [general-options|command-specified-options] [parameters]");
	_PRINT_STDERR("Commands:");
	_PRINT_STDERR("  -i  --install       Install a protocol type description file");
	_PRINT_STDERR("  -u  --update        Update/install a protocol type description file");
	_PRINT_STDERR("  -r  --remove        Remove a given protocol type");
	_PRINT_STDERR("  -l  --list-types    List all the types defined in the system");
	_PRINT_STDERR("  -T  --type-info     Show the information about the type");
	_PRINT_STDERR("  -S  --syntax-check  Validate the syntax of the ptype file");
	_PRINT_STDERR("  -h  --help          Show this help message");
	_PRINT_STDERR("  -v  --version       Show version of this program");
	_PRINT_STDERR("General Options:");
	_PRINT_STDERR("  -R  --db-prefix     The prefix of the database");
	_PRINT_STDERR("\nInstallation");
	_PRINT_STDERR("  protoman --install [general-options] [comamnd-sepecified-options]<ptype-file1> ... <ptype-fileN>");
	_PRINT_STDERR("  Command Specified options:");
	_PRINT_STDERR("    -d  --dry-run       Do not update the database, just simulate the operation");
	_PRINT_STDERR("    -p  --padding-size  Use the given number of bytes as the padding length");
	_PRINT_STDERR("    -q  --quiet         Quiet mode, do not output the installation information");
	_PRINT_STDERR("    -y  --yes           Do not prompt the confirmation information and continue");
	_PRINT_STDERR("\nUpdate");
	_PRINT_STDERR("  protoman --update [general-options] [command-specified-options] <ptype-file1> ... <ptype-fileN>");
	_PRINT_STDERR("  Command Specified options:");
	_PRINT_STDERR("    -d  --dry-run       Do not update the database, just simulate the operation");
	_PRINT_STDERR("    -f  --force         Force the operation be done and remove all the broken types caused by the operation");
	_PRINT_STDERR("    -p  --padding-size  Use the given number of bytes as the padding length");
	_PRINT_STDERR("    -q  --quiet         Quiet mode, do not output the installation information");
	_PRINT_STDERR("    -y  --yes           Do not prompt the confirmation information and continue");
	_PRINT_STDERR("\nRemove");
	_PRINT_STDERR("  protoman --remove [general-options] [command-specified-options] <type-name1> ... <type-nameN>");
	_PRINT_STDERR("  Command Specified options:");
	_PRINT_STDERR("    -d  --dry-run       Do not update the database, just simulate the operation");
	_PRINT_STDERR("    -q  --quiet         Quiet mode, do not output the installation information");
	_PRINT_STDERR("    -y  --yes           Do not prompt the confirmation information and continue");
	_PRINT_STDERR("\nList");
	_PRINT_STDERR("  protoman --list-types [general-options]");
	_PRINT_STDERR("\nShow Type Info");
	_PRINT_STDERR("  protoman --type-info [general-options] [command-specifeid-options] <type-name1> ... <tyname-nameN>");
	_PRINT_STDERR("  Command Specified options:");
	_PRINT_STDERR("    -B  --base-type     Also resolve the base type recursively");
	_PRINT_STDERR("\nSyntax Check");
	_PRINT_STDERR("  protoman --syntax-check [general-options]  <ptype-file1> ... <ptype-fileN>");
}
void display_version()
{
	_PRINT_STDERR("protoman: The Plumber centralized protocol type system manangement utilities");
	_PRINT_STDERR("Program Version       : " PLUMBER_VERSION);
}

int check_specified_options(const char* allowed, const int* options)
{
	int i;
	for(i = 0; i < 128; i ++)
	    if(options[i] && strchr(allowed, (char)i) == NULL)
	        return 0;
	return 1;
}

int parse_args(int argc, char** argv, program_option_t* out)
{
	static struct option options[] = {
		{"install"      ,       no_argument,        0,         'i'},
		{"update"       ,       no_argument,        0,         'u'},
		{"remove"       ,       no_argument,        0,         'r'},
		{"list-types"   ,       no_argument,        0,         'l'},
		{"type-info"    ,       no_argument,        0,         'T'},
		{"help"         ,       no_argument,        0,         'h'},
		{"version"      ,       no_argument,        0,         'v'},
		{"db-prefix"    , required_argument,        0,         'R'},
		{"force"        ,       no_argument,        0,         'f'},
		{"dry-run"      ,       no_argument,        0,         'd'},
		{"yes"          ,       no_argument,        0,         'y'},
		{"padding-size" , required_argument,        0,         'p'},
		{"quiet"        ,       no_argument,        0,         'q'},
		{"base-type"    ,       no_argument,        0,         'B'},
		{"syntax-check" ,       no_argument,        0,         'S'},
		{0              ,                 0,        0,          0 }
	};

	out->command = CMD_NOP;
	out->force = 0;
	out->dry_run = 0;
	out->default_yes = 0;
	out->db_root = NULL;
	out->target = NULL;
	out->show_base_type = 0;
	out->padding_size = sizeof(uintptr_t);

	static int seen_opts[128] = {};

#define _OPCASE(what, flag) \
	case what:\
	    if(out->command != CMD_NOP) ERROR_RETURN_LOG(int, "Command line param contains two commands");\
	    out->command = flag;\
	    break
	int opt_idx, c;
	for(;(c = getopt_long(argc, argv, "iurlThvR:fdyp:qBS", options, &opt_idx)) >= 0;)
	{
		if(c >= 0 && c < 128) seen_opts[c]++;
		switch(c)
		{
			_OPCASE('i', CMD_INSTALL);
			_OPCASE('u', CMD_UPDATE);
			_OPCASE('r', CMD_REMOVE);
			_OPCASE('l', CMD_LIST_TYPES);
			_OPCASE('T', CMD_SHOW_INFO);
			_OPCASE('h', CMD_HELP);
			_OPCASE('v', CMD_VERSION);
			_OPCASE('S', CMD_SYNTAX);
			case 'R':
			    out->db_root = optarg;
			    break;
			case 'f':
			    out->force = 1;
			    break;
			case 'd':
			    out->dry_run = 1;
			    break;
			case 'y':
			    out->default_yes = 1;
			    break;
			case 'p':
			    out->padding_size = (uint32_t)atoi(optarg);
			    break;
			case 'q':
			    log_level(ERROR);
			    break;
			case 'B':
			    out->show_base_type = 1;
			    break;
			default:
			    return ERROR_CODE(int);
		}
	}

#define CHECK_SPECIFIED_OPTIONS(cmd, allowed) \
	case cmd: \
	    if(!check_specified_options(allowed, seen_opts))\
	        ERROR_RETURN_LOG(int, "Invalid command specified options");\
	    break
	switch(out->command)
	{
		CHECK_SPECIFIED_OPTIONS(CMD_INSTALL, "iRdpqy");
		CHECK_SPECIFIED_OPTIONS(CMD_UPDATE,  "uRdfpqy");
		CHECK_SPECIFIED_OPTIONS(CMD_REMOVE,  "rRdy");
		CHECK_SPECIFIED_OPTIONS(CMD_LIST_TYPES,  "lR");
		CHECK_SPECIFIED_OPTIONS(CMD_SHOW_INFO,   "BTR");
		CHECK_SPECIFIED_OPTIONS(CMD_VERSION,    "v");
		CHECK_SPECIFIED_OPTIONS(CMD_SYNTAX,    "S");
		CHECK_SPECIFIED_OPTIONS(CMD_HELP,       "h");
		default:
		    break;
	}

	if(optind < argc)
	{
		out->target = (char const* const*)(argv + optind);
		out->target_count = (uint32_t)(argc - optind);
	}

	if(out->command == CMD_NOP)
	    ERROR_RETURN_LOG(int, "Missing an operation command");

	if((out->command & TARGET) && out->target == NULL)
	    ERROR_RETURN_LOG(int, "Missing operation target");

	return 0;
}

__attribute__((noreturn)) void properly_exit(int code)
{
	if(ERROR_CODE(int) == proto_finalize())
	    log_libproto_error(__FILE__, __LINE__);
	exit(code);
}
static program_option_t program_option;

int op_compare(const void* left, const void* right)
{
	const sandbox_op_t* lop = (const sandbox_op_t*)left;
	const sandbox_op_t* rop = (const sandbox_op_t*)right;

	if(lop->opcode != rop->opcode)
	    return (int)lop->opcode - (int)rop->opcode;

	return strcmp(lop->target, rop->target);
}

int confirm_operation(sandbox_t* sandbox, const program_option_t* option)
{
	sandbox_op_t ops[1024];
	_PRINT_STDERR("Validating....");
	if(ERROR_CODE(int) == sandbox_dry_run(sandbox, ops, sizeof(ops) / sizeof(ops[0])))
	    ERROR_RETURN_LOG(int, "Sandbox validation failed");
	uint32_t n, i, last_opcode = SANDBOX_NOMORE;
	for(n = 0; n < sizeof(ops) / sizeof(ops[0]) && ops[n].opcode != SANDBOX_NOMORE; n ++);
	qsort(ops, n, sizeof(sandbox_op_t), op_compare);


	for(i = 0; i < n; i ++)
	{
		static const char* opdesc[] = {
			[SANDBOX_CREATE] = "Types to create",
			[SANDBOX_DELETE] = "Types to delete",
			[SANDBOX_UPDATE] = "Types to update"
		};
		if(last_opcode != ops[i].opcode)
		{
			_PRINT_STDERR("%s:", opdesc[ops[i].opcode]);
			last_opcode = ops[i].opcode;
		}

		_PRINT_STDERR("\t[%u]\t%s", i, ops[i].target);
	}

	if(option->dry_run) return 0;

	int yes = option->default_yes ? 1 : -1;
	for(;yes == -1;)
	{
		fprintf(stderr, "Do you want to continue? [y/N] ");
		fflush(stderr);
		int ch = -1, cc;
		while(EOF != (cc = fgetc(stdin)))
		{
			if(cc == '\n') break;
			ch = cc;
		}
		if(ch == 'y' || ch == 'Y') yes = 1;
		else if(ch == 'n' || ch == 'N') yes = 0;
		if(ch == -1) yes = 0;
	}
	if(yes && sandbox_commit(sandbox) == ERROR_CODE(int))
	    ERROR_RETURN_LOG(int, "Cannot update the database");
	else if(yes) _PRINT_STDERR("Operation sucessfully posted");
	else _PRINT_STDERR("Modification reverted");
	return 0;
}
int do_remove(const program_option_t* option)
{
	sandbox_t* sandbox = sandbox_new(SANDBOX_INSERT_ONLY);
	uint32_t i;
	if(NULL == sandbox)
	    ERROR_RETURN_LOG(int, "Cannot create sandbox for the install command");
	for(i = 0; i < option->target_count; i ++)
	{
		if(ERROR_CODE(int) == sandbox_delete_type(sandbox, option->target[i]))
		    LOG_WARNING("Cannot insert deletion operation to sandbox");
	}

	if(confirm_operation(sandbox, option) == ERROR_CODE(int))
	    goto ERR;

	return sandbox_free(sandbox);
ERR:
	if(NULL != sandbox)
	    sandbox_free(sandbox);
	return 1;

}

int do_install(int is_update, const program_option_t* program_option)
{
	sandbox_insert_flags_t sf = is_update ? (program_option->force ?
	                                SANDBOX_FORCE_UPDATE :
	                                SANDBOX_ALLOW_UPDATE) :
	                            SANDBOX_INSERT_ONLY;
	sandbox_t* sandbox = sandbox_new(sf);
	uint32_t i;
	if(NULL == sandbox)
	    ERROR_RETURN_LOG(int, "Cannot create sandbox for the install command");

	for(i = 0; i < program_option->target_count; i ++)
	{
		compiler_result_t *result = NULL;
		lexer_t* lexer = lexer_new(program_option->target[i]);
		if(NULL == lexer)
		    ERROR_LOG_GOTO(ITER_ERR, "Cannot create lexer for file %s", program_option->target[i]);

		_PRINT_STDERR("Compiling type description file %s", program_option->target[i]);

		compiler_options_t opt = {
			.lexer = lexer,
			.padding_size = program_option->padding_size
		};

		if(NULL == (result = compiler_compile(opt)))
		    ERROR_LOG_GOTO(ITER_ERR, "Cannot compile the type description file");

		compiler_type_t* type;
		for(type = result->type_list; type != NULL; type = type->next)
		{
			proto_type_t* proto = type->proto_type;
			static char buf[1024];
			snprintf(buf, sizeof(buf), "%s/%s", type->package, type->name);
			if(sandbox_insert_type(sandbox, buf, proto) == ERROR_CODE(int))
			    ERROR_LOG_GOTO(ITER_ERR, "Cannot add protocol %s to sandbox", buf);
			else type->proto_type = NULL;  /* Because the sandbox steal the ownership */
		}

		if(ERROR_CODE(int) == compiler_result_free(result))
		    ERROR_LOG_GOTO(ITER_ERR, "Cannot dispose the compiler result");
		if(ERROR_CODE(int) == lexer_free(lexer))
		    ERROR_LOG_GOTO(ITER_ERR, "Cannot dispose the lexer");
		continue;
ITER_ERR:
		if(result != NULL)
		    compiler_result_free(result);
		if(lexer != NULL)
		    lexer_free(lexer);
		goto ERR;
	}

	if(confirm_operation(sandbox, program_option) == ERROR_CODE(int))
	    goto ERR;

	return sandbox_free(sandbox);
ERR:
	if(NULL != sandbox)
	    sandbox_free(sandbox);
	return 1;
}

static const char ext[] = ".proto";
int _filename_filter(const struct dirent* ent)
{
	if(ent->d_name[0] == '.') return 0;
	if(ent->d_type == DT_DIR) return 1;
	size_t len = strlen(ent->d_name);
	if(len > sizeof(ext) - 1 && strcmp(ext, ent->d_name + len - sizeof(ext) + 1) == 0)
	    return 1;
	return 0;
}

int do_list(char* bufptr, const program_option_t* option)
{
	static char pathbuf[PATH_MAX];
	static const char* pathbuf_end = &pathbuf[PATH_MAX];
	static const char* relpath_begin = NULL;
	if(bufptr == NULL)
	{
		snprintf(pathbuf, sizeof(pathbuf), "%s/", option->db_root);
		relpath_begin = bufptr = pathbuf + strlen(pathbuf);
	}

	int num_dirent, i;
	struct dirent **result = NULL;
	if((num_dirent = scandir(pathbuf, &result, _filename_filter, alphasort)) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot access directory %s", pathbuf);

	int rc = 0;

	for(i = 0; i < num_dirent; i ++)
	{
		const struct dirent* ent = result[i];
		    size_t len = strlen(ent->d_name);
		if(ent->d_type == DT_DIR)
		{
			if(bufptr + len > pathbuf_end - 2)
			    len = (size_t)(pathbuf_end - bufptr - 2);
			memcpy(bufptr, ent->d_name, len);
			bufptr[len] = '/';
			bufptr[len + 1] = 0;
			rc |= do_list(bufptr + len + 1, option);
		}
		else
		{
			if(bufptr + len - sizeof(ext) >= pathbuf_end - 1)
			    len = (size_t)(pathbuf_end - bufptr) - sizeof(ext);
			else
			    len -= sizeof(ext) - 1;
			memcpy(bufptr, ent->d_name, len);
			bufptr[len] = 0;
			_PRINT_INFO("%s", relpath_begin);
		}
	}

	goto CLEANUP;
ERR:
	rc = 1;
CLEANUP:
	if(NULL != result)
	{
		for(i = 0; i < num_dirent; i ++)
		    if(NULL != result[i])
		        free(result[i]);
		free(result);
	}
	return 0;
}

int show_type(const char* type, int rec)
{
	int rc = 0;
	static char pwdbuf[PATH_MAX];
	int n = snprintf(pwdbuf, sizeof(pwdbuf), "%s", type);
	if(n > PATH_MAX) n = PATH_MAX;
	for(;n > 0 && pwdbuf[n] != '/'; n--);
	pwdbuf[n] = 0;

	proto_err_clear();
	const proto_type_t* proto = proto_db_query_type(type);
	if(NULL == proto)
	{
		log_libproto_error(__FILE__, __LINE__);
		return 1;
	}

	if(rec)
	{
		const proto_type_entity_t* ent = proto_type_get_entity(proto, 0);

		if(ent != NULL && ent->symbol == NULL && ent->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
		{
			const char* refname = proto_ref_typeref_get_path(ent->type_ref);
			if(NULL == refname)  goto REC_ERR;
			const char* fullname = proto_cache_full_name(refname, pwdbuf);
			if(NULL == fullname) goto REC_ERR;
			rc |= show_type(fullname, 1);
			goto REC_EXIT;
REC_ERR:
			_PRINT_INFO("                      <libproto-error>");
			rc = 1;
			log_libproto_error(__FILE__, __LINE__);
		}
	}

REC_EXIT:
	_PRINT_INFO("\nTypeName: %s", type);
	_PRINT_INFO("     NameSapce:       %s", pwdbuf);
	_PRINT_INFO("     Size:            %u", proto_db_type_size(type));
	_PRINT_INFO("     Padding Size:    %u", proto_type_get_padding_size(proto));
	_PRINT_INFO("     Depends:");

	uint32_t j;
	uint32_t nent = proto_db_type_size(type);
	if(ERROR_CODE(uint32_t) == nent)
	{
		log_libproto_error(__FILE__, __LINE__);
		return 1;
	}
	for(j = 0; j < proto_type_get_size(proto); j ++)
	{
		const proto_type_entity_t* ent = proto_type_get_entity(proto, j);
		if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
		{
			const char* refname = proto_ref_typeref_get_path(ent->type_ref);
			if(NULL == refname)  goto ENT_ERR;
			const char* fullname = proto_cache_full_name(refname, pwdbuf);
			if(NULL == fullname) goto ENT_ERR;
			_PRINT_INFO("                      %s", fullname);
			continue;
ENT_ERR:
			_PRINT_INFO("                      <libproto-error>");
			rc = 1;
			log_libproto_error(__FILE__, __LINE__);
		}
	}

	_PRINT_INFO("     Reverse depends:");
	char const* const* rdeps = proto_cache_revdep_get(type, NULL);
	if(NULL == rdeps)
	{
		_PRINT_INFO("                      <libproto-error>");
		rc = 1;
		log_libproto_error(__FILE__, __LINE__);
	}
	for(j = 0; rdeps[j] != NULL; j ++)
	    _PRINT_INFO("                      %s", rdeps[j]);

	_PRINT_INFO("     Memory layout:");
	for(j = 0; j < proto_type_get_size(proto); j ++)
	{
		const proto_type_entity_t* ent = proto_type_get_entity(proto, j);
		uint32_t offset, size;
		const char* target = NULL;
		const char* symbol = NULL;
		const char* typename = NULL;
		static char buf[1024];
		if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_TYPE)
		{
			if(ent->symbol == NULL)
			{
				symbol = "<Base-Type>";
				offset = 0;
				const char* refname = proto_ref_typeref_get_path(ent->type_ref);
				if(NULL == refname)  goto LAYOUT_ERR;
				typename = proto_cache_full_name(refname, pwdbuf);
				if(NULL == typename) goto LAYOUT_ERR;
				size = proto_db_type_size(typename);
			}
			else
			{
				const char* refname = proto_ref_typeref_get_path(ent->type_ref);
				if(NULL == refname)  goto LAYOUT_ERR;
				typename = proto_cache_full_name(refname, pwdbuf);
				if(NULL == typename) goto LAYOUT_ERR;
				symbol = ent->symbol;
				offset = proto_db_type_offset(type, symbol, &size);
			}
		}
		else if(ent->header.refkind == PROTO_TYPE_ENTITY_REF_NAME)
		{
			symbol = ent->symbol;
			target = proto_ref_nameref_string(ent->name_ref, buf, sizeof(buf));
			offset = proto_db_type_offset(type, target, &size);
		}
		else
		{
			if(ent->symbol == NULL) continue;
			symbol = ent->symbol;
			offset = proto_db_type_offset(type, symbol, &size);
			static char primitive_buf[128];
			if(ent->metadata == NULL)
			{
				typename = "<primitive>";
			}
			else if(ent->metadata->flags.scope.valid)
			{
				snprintf(primitive_buf, sizeof(primitive_buf), "<%sruntime scope object: %s>",
				         ent->metadata->flags.scope.primitive ? "primitive " : "",
				         ent->metadata->scope_typename);
				typename = primitive_buf;
			}
			else
			{
				snprintf(primitive_buf, sizeof(primitive_buf), "<%s%sprimitive>",
				         ent->metadata->flags.numeric.is_signed ? "signed " : "unsigned ",
				         ent->metadata->flags.numeric.is_real   ? "float-point " : "interger ");
				typename = primitive_buf;
			}

		}

		if(ERROR_CODE(uint32_t) == offset) goto LAYOUT_ERR;
		if(target == NULL)
		{
			printf("                      0x%.8x - 0x%.8x:\t%s :: {%s", offset, offset + size, symbol, typename);
			if(ent->header.dimlen > 1 || ent->dimension[0] > 1)
			{
				uint32_t k;
				for(k = 0; k < ent->header.dimlen; k ++)
				    printf("[%u]", ent->dimension[k]);
			}
			else if(ent->header.metadata && !ent->metadata->flags.numeric.invalid)
			{
				if(ent->metadata->flags.numeric.default_size > 0)
				{
					uint32_t size = ent->metadata->flags.numeric.default_size;
					uint32_t real = ent->metadata->flags.numeric.is_real;
					uint32_t sign = ent->metadata->flags.numeric.is_signed;
#define _PRINT_DEF(sz, r, s, f, t) else if(size == sz && real == r && sign == s) printf(" = %"f, *(t*)ent->metadata->numeric_default);
					if(0);
					_PRINT_DEF(1, 0, 0, PRIu8, uint8_t)
					_PRINT_DEF(1, 0, 1, PRId8, int8_t)
					_PRINT_DEF(2, 0, 0, PRIu16, uint16_t)
					_PRINT_DEF(2, 0, 1, PRId16, int16_t)
					_PRINT_DEF(4, 0, 0, PRIu32, uint32_t)
					_PRINT_DEF(4, 0, 1, PRId32, int32_t)
					_PRINT_DEF(4, 1, 1, "g", float)
					_PRINT_DEF(8, 0, 0, PRIu64, uint64_t)
					_PRINT_DEF(8, 0, 1, PRId64, int64_t)
					_PRINT_DEF(8, 1, 1, "lg", double)
					else printf(" = <unrecognized value>");
				}
			}
			puts("}");
		}
		else
		    printf("           [Alias] => 0x%.8x - 0x%.8x:\t%s -> %s\n", offset, offset + size, symbol, target);
		continue;

LAYOUT_ERR:
		_PRINT_INFO("                      <libproto-error>");
		rc = 1;
		log_libproto_error(__FILE__, __LINE__);
	}

	return rc;
}
int show_info(const program_option_t* option)
{
	int rc = 0;
	uint32_t i;
	for(i = 0; i < option->target_count; i ++)
	{
		rc |= show_type(option->target[i], option->show_base_type);
	}

	return rc;
}

int do_syntax(const program_option_t* option)
{
	uint32_t i;
	for(i = 0; i < option->target_count; i ++)
	{
		compiler_result_t *result = NULL;
		lexer_t* lexer = lexer_new(option->target[i]);
		if(NULL == lexer)
		    ERROR_LOG_GOTO(ITER_ERR, "Cannot create lexer for file %s", option->target[i]);

		_PRINT_STDERR("Checking syntax for %s", option->target[i]);

		compiler_options_t opt = {
			.lexer = lexer,
			.padding_size = sizeof(uintptr_t)
		};

		if(NULL == (result = compiler_compile(opt)))
		    ERROR_LOG_GOTO(ITER_ERR, "Cannot compile the type description file");

		if(ERROR_CODE(int) == compiler_result_free(result))
		    LOG_ERROR("Cannot dispose the compiler result");
		if(ERROR_CODE(int) == lexer_free(lexer))
		    LOG_ERROR("Cannot dispose the lexer");
		continue;
ITER_ERR:
		return 1;
	}
	return 0;
}

int main(int argc, char** argv)
{
	if(ERROR_CODE(int) == parse_args(argc, argv, &program_option))
	{
		display_help();
		exit(1);
	}

	if(ERROR_CODE(int) == proto_init())
	    LOG_LIBPROTO_ERROR_RETURN(int);

	int ret_code = 0;

	if(program_option.db_root != NULL && proto_cache_set_root(program_option.db_root) == ERROR_CODE(int))
	    LOG_LIBPROTO_ERROR_RETURN(int);

	if(NULL == (program_option.db_root = proto_cache_get_root()))
	    LOG_LIBPROTO_ERROR_RETURN(int);

	switch(program_option.command)
	{
		case CMD_VERSION:
		    display_version();
		    properly_exit(0);
		case CMD_HELP:
		    display_help();
		    properly_exit(0);
		case CMD_INSTALL:
		    ret_code = do_install(0, &program_option);
		    break;
		case CMD_UPDATE:
		    ret_code = do_install(1, &program_option);
		    break;
		case CMD_LIST_TYPES:
		    ret_code = do_list(NULL, &program_option);
		    break;
		case CMD_SHOW_INFO:
		    ret_code = show_info(&program_option);
		    break;
		case CMD_REMOVE:
		    ret_code = do_remove(&program_option);
		    break;
		case CMD_SYNTAX:
		    ret_code = do_syntax(&program_option);
		    break;
		default:
		    display_help();
		    properly_exit(1);
	}
	properly_exit(ret_code);
}
