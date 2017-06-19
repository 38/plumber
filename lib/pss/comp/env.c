/**
 * Copyright (C) 2017, Feng Liu
 * Copyright (C) 2017, Hao Hou
 **/
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>

#include <error.h>

#include <utils/hash/murmurhash3.h>
#include <package_config.h>

#include <pss/bytecode.h>
#include <pss/value.h>
#include <pss/frame.h>
#include <pss/log.h>
#include <pss/comp/env.h>

typedef struct _reg_t _reg_t;

typedef struct _var_t _var_t;

/**
 * @brief The data structure represents a register allocation
 * @note  We don't need a double linked list at this point for the hash table, because
 *        When we remove all the scope register allocations, the inner scope variable
 *        must be removed before the outter one, which means, the we always remove the
 *        first element in the var reglist
 **/
typedef struct _reg_t {
	pss_bytecode_regid_t regsn;         /*!< The register serial number */
	uint32_t             scope;         /*!< The scope id */
	_var_t*              var;           /*!< The backward pointer to the var node */
	_reg_t*              next;          /*!< The next node in the variable mapping table */
	_reg_t*              scope_next;    /*!< The next node shares the same scope */
} _reg_t;

/**
 * @brief The data structure used to manage all the variables that in the same name in different scopes
 **/
typedef struct _var_t {
	char*    name;               /*!< The variable name */
	uint64_t hash[2];            /*!< hash value of var name */
	_var_t*   next;              /*!< next var node that has the same hash index */
	_reg_t*   reglist;           /*!< The register allocation list, the first element should be the latest scope*/
} _var_t;

/**
 * @brief The actual data structure for a environment abstraction
 **/
struct _pss_comp_env_t {
	pss_bytecode_regid_t rheap_size;                               /*!< The size of the register recycle heap */
	pss_bytecode_regid_t rheap[(pss_bytecode_regid_t)-1];          /*!< The register recycle heap */
	_var_t*              vmap[PSS_COMP_ENV_HASH_SIZE];             /*!< The varable allocation map */
	pss_bytecode_regid_t next_unalloc;                             /*!< The next unallocated register */
	uint32_t             scope_level;                              /*!< The current scope level */
	_reg_t*              scope_regs[PSS_COMP_ENV_SCOPE_MAX];       /*!< The local register allocations for current scope */
};

/**
 * @brief Performe the heapify operation, which adjust the heap to maintain heap property
 * @param env The environment to adjust
 * @param root The root the adjust
 * @return Nothing
 **/
void _rheapify(pss_comp_env_t* env, pss_bytecode_regid_t root)
{
	for(;root < env->rheap_size;)
	{
		uint32_t min_idx = root;
		if(root * 2 + 1 < env->rheap_size && env->rheap[min_idx] > env->rheap[root * 2 + 1])
			min_idx = root * 2u + 1u;
		if(root * 2 + 2 < env->rheap_size && env->rheap[min_idx] > env->rheap[root * 2 + 2])
			min_idx = root * 2u + 2u;
		if(root == min_idx) break;
		pss_bytecode_regid_t tmp = env->rheap[root];
		env->rheap[root] = env->rheap[min_idx];
		env->rheap[min_idx] = tmp;
	}
}

/**
 * @brief Decrase the value of the given node
 * @param node The node index
 * @param env The environment to adjust
 * @return nothing
 **/
void _rheap_decrease(pss_comp_env_t* env, pss_bytecode_regid_t node)
{
	for(;node > 0 && env->rheap[node] < env->rheap[(node - 1) / 2]; node = (pss_bytecode_regid_t)((node - 1u) / 2u))
	{
		pss_bytecode_regid_t tmp = env->rheap[node];
		env->rheap[node] = env->rheap[(node - 1) / 2];
		env->rheap[(node - 1) / 2] = tmp;
	}
}

/**
 * @brief Get the next fastest register from the register map
 * @param env The environment to operate
 * @return The register serial number
 * @note This function will assume that the environment have at least one register is being recycled at this time
 **/
pss_bytecode_regid_t _rheap_get(pss_comp_env_t* env)
{
	pss_bytecode_regid_t ret = env->rheap[0];
	env->rheap[0] = env->rheap[--env->rheap_size];
	_rheapify(env, 0);
	return ret;
}

/**
 * @brief Put a register to the recycle heap
 * @param env The environment to operate
 * @param sn The serial number
 * @return status code
 **/
int _rheap_put(pss_comp_env_t* env, pss_bytecode_regid_t sn)
{
	env->rheap[env->rheap_size ++] = sn;
	_rheap_decrease(env, (pss_bytecode_regid_t)(env->rheap_size - 1u));
	return 0;
}

pss_comp_env_t* pss_comp_env_new()
{
	pss_comp_env_t* env = (pss_comp_env_t*)calloc(1, sizeof(pss_comp_env_t));
	if(NULL == env) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the abstract environment");
	return env;
}


int pss_comp_env_free(pss_comp_env_t* env)
{
	if(NULL == env) ERROR_RETURN_LOG(int, "Invalid arguments");

	uint32_t i;
	for(i = 0; i < PSS_COMP_ENV_HASH_SIZE; i ++)
	{
		_var_t* var;
		for(var = env->vmap[i]; NULL != var; )
		{
			_var_t* this = var;
			var = var->next;
			_reg_t* reg = this->reglist;
			for(;NULL != reg;)
			{
				_reg_t* this = reg;
				reg = reg->next;
				free(this);
			}
			free(this->name);
			free(this);
		}
	}

	free(env);
	return 0;
}

int pss_comp_env_open_scope(pss_comp_env_t* env)
{
	if(NULL == env) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(env->scope_level > PSS_COMP_ENV_SCOPE_MAX)
		ERROR_RETURN_LOG(int, "Too many nested scopes");

	env->scope_regs[env->scope_level ++] = NULL;

	return 0;
}

int pss_comp_env_close_scope(pss_comp_env_t* env)
{
	if(NULL == env) ERROR_RETURN_LOG(int, "Invalid arguments");

	if(env->scope_level == 0)
		ERROR_RETURN_LOG(int, "The environment is not in scope");

	_reg_t* reg;
	for(reg = env->scope_regs[env->scope_level - 1]; NULL != reg;)
	{
		_reg_t* this = reg;
		reg = reg->scope_next;

		this->var->reglist = this->next;
		if(ERROR_CODE(int) == _rheap_put(env, this->regsn))
			ERROR_RETURN_LOG(int, "Cannot put the released register to the recycle heap");

		free(this);
	}

	env->scope_level --;

	return 0;
}
/**
 * @brief Get the string hash code
 * @param str The input string
 * @param full The buffer used to return the full 128 bits hash
 * @return The hash slot id
 **/
static inline uint32_t _hash(const char* str, uint64_t full[2])
{
	static const uint32_t multiplier = (((uint32_t)((1ull << 63) % PSS_COMP_ENV_HASH_SIZE)) * 2) % PSS_COMP_ENV_HASH_SIZE;
	size_t len = strlen(str);
	murmurhash3_128(str, len, 0xf37d543fu, full);
	return (uint32_t)(((multiplier * (full[1] % PSS_COMP_ENV_HASH_SIZE)) + (full[0] % PSS_COMP_ENV_HASH_SIZE)) % PSS_COMP_ENV_HASH_SIZE);
}

/**
 * @brief Allocate the register from the given environment
 * @param env The environment
 * @return The register serial number
 **/
static inline pss_bytecode_regid_t _regalloc(pss_comp_env_t* env)
{
	if(env->rheap_size > 0) return _rheap_get(env);

	if(env->next_unalloc == ERROR_CODE(pss_bytecode_regid_t))
		ERROR_RETURN_LOG(pss_bytecode_regid_t, "Insufficient number of registers");

	return env->next_unalloc ++;
}

int pss_comp_env_get_var(pss_comp_env_t* env, const char* varname, int create, pss_bytecode_regid_t* regbuf)
{
	if(NULL == env || NULL == varname || NULL == regbuf)
		ERROR_RETURN_LOG(int, "Invalid arguments");

	if(env->scope_level == 0)
		ERROR_RETURN_LOG(int, "We are currently not in a scope");
	
	uint64_t hash[2];
	uint32_t slot = _hash(varname, hash);
	
	_var_t* var  = env->vmap[slot];
	/* TODO: Determine if we need to do the actual string compare */
	for(;NULL != var && (hash[0] != var->hash[0] || hash[1] != var-> hash[1]); var = var->next);

	if(!create)
	{
		if(NULL == var || NULL == var->reglist) return 0;
		*regbuf = pss_frame_serial_to_regid(var->reglist->regsn);
		return 1;
	}

	while(NULL == var)
	{
		var = (_var_t*)malloc(sizeof(*var));
		if(NULL == var)
			ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate new var node");

		size_t len = strlen(varname) + 1;
		if(NULL == (var->name = (char*)malloc(len)))
			ERROR_LOG_ERRNO_GOTO(VAR_CREATE_ERR, "Cannot allocate name buffer for the new var node");

		memcpy(var->name, varname, len);
		var->hash[0] = hash[0];
		var->hash[1] = hash[1];
		var->next = env->vmap[slot];
		var->reglist = NULL;
		env->vmap[slot] = var;
		break;
VAR_CREATE_ERR:
		free(var);
		return ERROR_CODE(int);
	}

	if(var->reglist != NULL && var->reglist->scope == env->scope_level)
		ERROR_RETURN_LOG(int, "Redefined variable in scope %s", varname);

	_reg_t* reg = (_reg_t*)malloc(sizeof(*reg));
	if(NULL == reg) ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate regiester node");

	if(ERROR_CODE(pss_bytecode_regid_t) == (reg->regsn = _regalloc(env)))
		ERROR_LOG_GOTO(CREATE_REG_ERR, "Cannot get next available register");

	reg->var = var;

	reg->next = var->reglist;
	var->reglist = reg;

	reg->scope_next = env->scope_regs[env->scope_level - 1];
	env->scope_regs[env->scope_level - 1] = reg;
	reg->scope = env->scope_level;

	*regbuf = pss_frame_serial_to_regid(reg->regsn);

	return 1;

CREATE_REG_ERR:
	free(reg);

	return ERROR_CODE(int);
}

pss_bytecode_regid_t pss_comp_env_mktmp(pss_comp_env_t* env)
{
	if(NULL == env) ERROR_RETURN_LOG(pss_bytecode_regid_t, "Invalid arguments");

	pss_bytecode_regid_t sn = _regalloc(env);

	if(ERROR_CODE(pss_bytecode_regid_t) == sn) 
		return sn;
	return pss_frame_serial_to_regid(sn);
}

int pss_comp_env_rmtmp(pss_comp_env_t* env, pss_bytecode_regid_t tmp)
{
	if(NULL == env || ERROR_CODE(pss_bytecode_regid_t) == tmp)
		ERROR_RETURN_LOG(pss_bytecode_regid_t, "Invalid arguments");

	return _rheap_put(env, pss_frame_regid_to_serial(tmp));
}
