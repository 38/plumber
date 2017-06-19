/**
 * Copyright (C) 2017, Feng Liu
 **/
#include <string.h>

#include <utils/hash/murmurhash3.h>
#include <pss/comp/env.h>

#define _MAP_LEN 32

struct _reg_node_t {
	pss_bytecode_regid_t regid;
	uint32_t             scope_id; /*!< scope id */
	struct _reg_node_t   *pre;     /*!< previous node in _var_node_t */
	struct _reg_node_t   *next;    /*!< next node in var_reg_map_t */
	struct _reg_node_t   *next_in_scope; /*!< next node in _scope_t */
};

struct _var_node_t {
	// @todo: may need to compare the strings
	char *var_name;              /*!< variable name */
	uint64_t hash[2];            /*!< hash value of var name */
	struct _var_node_t *next;    /*!< next var node that has the same hash index */
	struct _reg_node_t reg_head; /*!< register node list header, list is sorted by decreased scope*/
};

struct _var_node_head_t {
	struct _var_node_t *next; /*!< var nodes list */
};

struct _var_reg_map_t {
	struct _var_node_head_t var_node_head;
	struct _var_reg_map_t *next; /*!< next _var_reg */
};

struct _scope_t {
	uint32_t           id;    /*!< scope id, start from 0 */
	struct _scope_t    *pre;        /*!< previous scope */
	struct _reg_node_t *reg_list;   /*!< used register list */
	int                new_closure; /*!< if this scope is a new closure */
};

struct _pss_comp_env_t {
	pss_bytecode_regid_t reg_heap[~(pss_bytecode_regid_t)0]; /*!< unused register map */
	struct _var_reg_map_t map[_MAP_LEN];     /*!< map var name to register */
	pss_bytecode_regid_t last_unallocated;   /*!< last unallocated register */
	struct _scope_t *scope;                  /*!< scope link */
};

void _heap_init(pss_bytecode_regid_t reg_heap[])
{
	for(pss_bytecode_regid_t i
}

void _heap_insert(pss_bytecode_regid_t reg_heap[], pss_bytecode_regid_t regid)
{
}

pss_comp_env_t* pss_comp_env_new()
{
	pss_comp_env_t* env = (pss_comp_env_t*)calloc(sizeof(pss_comp_env_t));
	env->scope = (struct _scope_t*)calloc(sizeof(struct _scope_t));
	return env;
}


/**
 * scope must not be NULL
 */
void _free_scope(struct _scope_t* scope)
{
	struct _reg_node_t *next_reg, *reg = scope->reg_list;
	while(reg)
	{
		next_reg = reg->next_in_scope;
		// delete register from the double list in _var_reg_map_t
		reg->pre->next = reg->next;
		free(reg);
		reg = next_reg;
	}
	free(scope);
}


int pss_comp_env_free(pss_comp_env_t* env)
{
	if(NULL == env)
		return 0;
	struct _scope_t *pre_scope, *scope = env->scope;
	while(scope)
	{
		pre_scope = scope->pre;
		_free_scope(scope);
		scope = pre_scope;
	}
	free(env);
	return 0;
}

#define _CHCK_ENV(type) \
	if(NULL == env) \
		ERROR_RETURN_LOG(type, "Null env pointer");

int pss_comp_env_open_scope(pss_comp_env_t* env, int new_closure)
{
	_CHCK_ENV(int);
	struct _scope_t *scope = (struct _scope_t*)calloc(sizeof(struct _scope_t));
	scope->id = env->scope->id + 1;
	scope->new_closure = new_closure;
	scope->pre = env->scope;
}

int pss_comp_env_close_scope(pss_comp_env_t* env)
{
	_CHCK_ENV(int);
	struct _scope_t *pre_scope, *scope = env->scope;
	if(NULL != scope && NULL != scope->pre)
	{
		pre_scope = scope->pre;
		_free_scope(scope);
		env->scope = pre_scope;
	}
	return 0;
}

int pss_comp_env_get_var(pss_comp_env_t* env, const char* var, int create, pss_bytecode_regid_t* regbuf)
{
	_CHCK_ENV(int);
	uint64_t hash[2];
	murmurhash3_128(var, strlen(var), 0xf37d543fu, hash);
	uint32_t index = hash[0] % _MAP_LEN;

	struct _var_node_t *var_node = env->map[index].var_node_head.next;
	struct _reg_node_t *reg = NULL;

	while(var_node)
	{
		// @todo: also compare the var name string
		if(var_node->hash[0] == hash[0] && var_node->hash[1] == hash[1])
		{
			reg = var_node->reg_head.next;
			// found in current scope
			if(reg->scope_id == env->scope_id)
			{
				*regbuf = reg->regid;
				return 1;
			}
			// create a local variable
			if(create)
			{
				reg = (struct _reg_node_t*)calloc(sizeof(struct _reg_node_t));
				reg->reg_id = hea
				return 1;
			}
			else // access the variable in globals
			{
				*regbuf = reg->regid;
				return 0;
			}
		}
		var_node = var_node->next;
	}

	// create a local variable
	struct _var_node_t *v_node = (struct _var_node_t*)calloc(sizeof(struct _var_node_t));
}

