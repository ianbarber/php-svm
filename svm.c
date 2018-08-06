/*
  +----------------------------------------------------------------------+
  | PHP Version 7 / svm                                                  |
  +----------------------------------------------------------------------+
  | Copyright (c) 2010 Ian Barber                                        |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Authors: Ian Barber <ian.barber@gmail.com>                           |
  +----------------------------------------------------------------------+
*/

#include "php_svm.h"
#include "php_svm_internal.h"
#include "php_ini.h" /* needed for 5.2 */
#include "Zend/zend_exceptions.h"
#include "ext/standard/info.h"

static zend_class_entry *php_svm_sc_entry;
static zend_class_entry *php_svm_model_sc_entry;
static zend_class_entry *php_svm_exception_sc_entry;

static zend_object_handlers svm_object_handlers;
static zend_object_handlers svm_model_object_handlers;

static int problemSize;

#ifndef TRUE
#       define TRUE 1
#       define FALSE 0
#endif

#define SUCCESS 0

#define SVM_MAX_LINE_SIZE 4096
#define SVM_THROW(message, code) \
		zend_throw_exception(php_svm_exception_sc_entry, message, code); \
		return;

#define SVM_ERROR_MSG_SIZE 512			
#define SVM_THROW_LAST_ERROR(fallback, code) \
		zend_throw_exception(php_svm_exception_sc_entry, (strlen(intern->last_error) ? intern->last_error : fallback), code); \
		memset(intern->last_error, 0, SVM_ERROR_MSG_SIZE); \
		return;			

typedef enum SvmLongAttribute {
	SvmLongAttributeMin = 100,
	phpsvm_svm_type,
	phpsvm_kernel_type,
	phpsvm_degree,
	SvmLongAttributeMax /* Always add before this */
} SvmLongAttribute;

typedef enum SvmDoubleAttribute {
	SvmDoubleAttributeMin = 200,
	phpsvm_gamma,
	phpsvm_nu,
	phpsvm_eps,
	phpsvm_p,
	phpsvm_coef0,
	phpsvm_C,
	phpsvm_cache_size,
	SvmDoubleAttributeMax /* Always add before this */
} SvmDoubleAttribute;

typedef enum SvmBoolAttribute {
	SvmBoolAttributeMin = 300,
	phpsvm_shrinking,
	phpsvm_probability,
	SvmBoolAttributeMax /* Always add before this */
} SvmBoolAttribute;

/* ---- START HELPER FUNCS ---- */

static void print_null(const char *s) {}

static zend_bool php_svm_set_bool_attribute(php_svm_object *intern, SvmBoolAttribute name, zend_bool value) /*{{{*/
{
	if (name >= SvmBoolAttributeMax) {
		return FALSE;
	}
	
	switch (name) {
		case phpsvm_shrinking:
			intern->param.shrinking = value == TRUE ? 1 : 0;
			break;
		case phpsvm_probability:
			intern->param.probability = value == TRUE ? 1 : 0;
			break;
		default:
			return FALSE;
	}

	return TRUE;
}/*}}}*/

static zend_bool php_svm_set_double_attribute(php_svm_object *intern, SvmDoubleAttribute name, double value) /*{{{*/
{
	if (name >= SvmDoubleAttributeMax) {
		return FALSE;
	}
	
	switch (name) {
		case phpsvm_gamma:
			intern->param.gamma = value;
			break;
		case phpsvm_nu:
			intern->param.nu = value;
			break;
		case phpsvm_eps:
			intern->param.eps = value;
			break;
		case phpsvm_cache_size:
			intern->param.cache_size = value;
			break;
		case phpsvm_p:
			intern->param.p = value;
			break;
		case phpsvm_coef0:
			intern->param.coef0 = value;
			break;
		case phpsvm_C:
			intern->param.C = value;
			break;
		default:
			return FALSE;
	}
	
	return TRUE;
}/*}}}*/

static zend_bool php_svm_set_long_attribute(php_svm_object *intern, SvmLongAttribute name, zend_long value) /*{{{*/
{
	if (name >= SvmLongAttributeMax) {
		return FALSE;
	}

	switch (name) {
		case phpsvm_svm_type:
			if( value != C_SVC &&
				value != NU_SVC && 
				value != ONE_CLASS && 
				value != EPSILON_SVR && 
				value != NU_SVR ) {
					return FALSE;
			}
			intern->param.svm_type = (int)value;
			break;
		case phpsvm_kernel_type:
			if( value != LINEAR &&
				value != POLY && 
				value != RBF && 
				value != SIGMOID && 
				value != PRECOMPUTED ) {
					return FALSE;
			}
			intern->param.kernel_type = (int)value;
			break;
		case phpsvm_degree:
			intern->param.degree = (int)value;
			break;
		default:
			return FALSE;
	}
	
	return TRUE;
}/*}}}*/

/** {{{ zend_bool php_svm_stream_to_array(php_svm_object *intern, php_stream *stream, zval *retval)
	Take a stream containing lines of SVMLight format data and convert them into a PHP array for use by the training
	function.
*/
static zend_bool php_svm_stream_to_array(php_svm_object *intern, php_stream *stream, zval *retval)
{
	while (!php_stream_eof(stream)) {
		char buf[SVM_MAX_LINE_SIZE];
		size_t retlen = 0;
		int line = 1;
		
		/* Read line by line */
		if (php_stream_get_line(stream, buf, SVM_MAX_LINE_SIZE, &retlen)) {
		
			zval line_array, pz_label;
			char *label, *ptr, *l = NULL;

			ptr   = buf;
			label = php_strtok_r(ptr, " \t", &l);

			if (!label) {
			    snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Incorrect data format on line %d", line);
				return FALSE;
			}
			
			/* The line array */
			//MAKE_STD_ZVAL(line_array);
			array_init(&line_array);

			/* The label */
			ZVAL_STRING(&pz_label, label);
			convert_to_double(&pz_label);

			/* Label is the first item in the line array */
			add_next_index_zval(&line_array, &pz_label);
			
			/* Read rest of the values on the line */
			while (1) {
				char *idx, *value;
				zval pz_idx, pz_value;
				
				/* idx:value format */
				idx   = php_strtok_r(NULL, ":", &l);
				value = php_strtok_r(NULL, " \t", &l);
			
				if (!value) {
					break;
				}
				
				/* Make zvals and convert to correct types */
				ZVAL_STRING(&pz_idx, idx);
				convert_to_long(&pz_idx);
				
				ZVAL_STRING(&pz_value, value);
				convert_to_double(&pz_value);
				
				add_index_zval(&line_array, Z_LVAL(pz_idx), &pz_value);
				zval_dtor(&pz_idx);
			}
			add_next_index_zval(retval, &line_array);
			line++;
		}
	}
	return TRUE;
}
/* }}} */


/* {{{ int _php_count_values(zval *array);
For a an array of arrays, count the number of items in all subarrays. 
*/
static int _php_count_values(zval *array)
{
	int values = 0;
	zval *val;

	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(array), val) {
		if (Z_TYPE_P(val) == IS_ARRAY) {
			values += zend_hash_num_elements(Z_ARRVAL_P(val));
		}
	} ZEND_HASH_FOREACH_END();


	return values;
}
/* }}} */

/* {{{ static void php_svm_free_problem(struct svm_problem *problem) {
Free the generated problem.
*/
static void php_svm_free_problem(struct svm_problem *problem) {
	if (problem->x)	{
		efree(problem->x);
	}
		
	if (problem->y) {
		efree(problem->y);
	}
	
	if (problem) {
		efree(problem);
	}
}
/* }}} */

/* {{{ static zend_bool php_svm_read_array(php_svm_object *intern, php_svm_model_object *intern_model, zval *array)
Take a PHP array, and prepare libSVM problem data for training with. 
*/
static struct svm_problem* php_svm_read_array(php_svm_object *intern, php_svm_model_object **intern_model_ptr, zval *array, zval * rzval)
{
	zval *pzval;
	char *err_msg = NULL;
	zend_string *key;
	char *endptr;
	int i, num_labels, elements;
	int j = 0, max_index = 0, inst_max_index = 0;
	zend_ulong index;
	struct svm_problem *problem;
	//zval svm_mo;
	zend_object * zobj;
	php_svm_model_object * intern_model = NULL;

	/* total number of elements */
	elements = _php_count_values(array);

	problemSize = elements;

	if (intern_model)
	{
		/* If reading multiple times make sure that we don't leak */
		if (intern_model->x_space) {
			efree(intern_model->x_space);
			intern_model->x_space = NULL;
		}
		if (intern_model->model) {
			#if LIBSVM_VERSION >= 300
				svm_free_and_destroy_model(&intern_model->model);
			#else
				svm_destroy_model(intern_model->model);
			#endif
		    
			intern_model->model = NULL;
		}
	} else {
		// create model object
		object_init_ex(rzval, php_svm_model_sc_entry);

		zobj = Z_OBJ_P(rzval);
		intern_model = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));	
	}
	

	/* Allocate the problem */
	problem = emalloc(sizeof(struct svm_problem));
	
	/* x and y */
	num_labels = zend_hash_num_elements(HASH_OF(array));
	
	/* Allocate space for the labels */
	problem->y = emalloc(num_labels * sizeof(double));
	
	/* allocate space for x */
	problem->x = emalloc(num_labels * sizeof(struct svm_node *));
	

	/* How many labels */
	problem->l = num_labels;

	i = 0;
	/* Fill the problem */
	ZEND_HASH_FOREACH_VAL(Z_ARRVAL_P(array), pzval) {

		zval *pz_label;
	
		if (Z_TYPE_P(pzval) != IS_ARRAY) {
			err_msg = "Data format error";
			goto return_error;
		}	
						
		if (zend_hash_num_elements(Z_ARRVAL_P(pzval)) < 2) {
			err_msg = "Wrong amount of nodes in the sub-array";
			goto return_error;
		}
		
		problem->x[i] = &intern_model->x_space[j];
		
		zend_hash_internal_pointer_reset(Z_ARRVAL_P(pzval));
		
		if ((pz_label = zend_hash_get_current_data_ex(Z_ARRVAL_P(pzval),  &(Z_ARRVAL_P(pzval))->nInternalPointer)) != NULL) {
			
			if (Z_TYPE_P(pz_label) != IS_DOUBLE) {
				convert_to_double(pz_label);
			}
			problem->y[i] = Z_DVAL_P(pz_label);
		} else {
			err_msg = "The sub-array contains only the label. Missing index-value pairs";
			goto return_error;
		}

		while (1) {
			zval *pz_value;

			if ((zend_hash_move_forward(Z_ARRVAL_P(pzval)) == SUCCESS) && 
				((pz_value = zend_hash_get_current_data(Z_ARRVAL_P(pzval))) != NULL)) {						

				if (zend_hash_get_current_key(Z_ARRVAL_P(pzval), &key, &index) == HASH_KEY_IS_STRING) {
					intern_model->x_space[j].index = (int) strtol(ZSTR_VAL(key), &endptr, 10);
				} else {
					intern_model->x_space[j].index = (int) index;
				}
				
				if (Z_TYPE_P(pz_value) != IS_DOUBLE) {
					convert_to_double(pz_value);
				}
				intern_model->x_space[j].value = Z_DVAL_P(pz_value);
			
				inst_max_index = intern_model->x_space[j].index;
				j++;
			} else {
				break;
			}
		}
		
		intern_model->x_space[j++].index = -1;

		if (inst_max_index > max_index) {
			max_index = inst_max_index;
		}
		i++;
	} ZEND_HASH_FOREACH_END();
	
	if (intern->param.gamma == 0 && max_index > 0) {
		intern->param.gamma = 1.0/max_index;
	}

	*intern_model_ptr = intern_model;
	
	return problem;
	
return_error:
	php_svm_free_problem(problem);
	if (err_msg) {
		snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "%s", err_msg);
	}
		
	return NULL;
}
/* }}} */

/* {{{ static zend_bool php_svm_train(php_svm_object *intern, php_svm_model_object *intern_model, struct svm_problem *problem) 
Train based on a libsvm problem structure
*/
static zend_bool php_svm_train(php_svm_object *intern, php_svm_model_object *intern_model, struct svm_problem *problem) 
{
	const char *err_msg = NULL;
	err_msg = svm_check_parameter(problem, &(intern->param));
	if (err_msg) {
		snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "%s", err_msg);
		return FALSE;
	}

	intern_model->model = svm_train(problem, &(intern->param));

	/* Failure ? */
	if (!intern_model->model) {
		snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Failed to train using the data");
		return FALSE;
	}
	
	return TRUE;
}
/* }}} */

/* {{{ static zval* php_svm_get_data_from_param(php_svm_object *intern, zval *zparam)
Take an incoming parameter and convert it into a PHP array of svmlight style data.
*/
static int php_svm_get_data_from_param(php_svm_object *intern, zval *zparam, zval ** data_ptr) 
{
	zend_bool our_stream = 0;
	zend_bool need_read = 1;
	php_stream *stream = NULL;
	
	switch (Z_TYPE_P(zparam)) {		
		case IS_STRING:
			stream = php_stream_open_wrapper(Z_STRVAL_P(zparam), "r", REPORT_ERRORS, NULL);
			our_stream = 1;
		break;
		
		case IS_RESOURCE:
			php_stream_from_zval_no_verify(stream, zparam);
			our_stream = 0;
		break;
		
		case IS_ARRAY:
			our_stream = 0;
			need_read  = 0;
		break;
		
		default:
			snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Incorrect parameter type, expecting string, stream or an array");
			return FALSE;
		break;
	}

	/* If we got stream then read it in */
	if (need_read) {
		if (!stream) {
			snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Failed to open the data file");
			return FALSE;
		}
	
		if (!php_svm_stream_to_array(intern, stream, *data_ptr)) {
			zval_dtor(*data_ptr);
			efree(data_ptr);
			if (our_stream) {
				php_stream_close(stream);
			}
			snprintf(intern->last_error, SVM_ERROR_MSG_SIZE, "Failed to read the data");
			return FALSE;
		}
	} else {
		*data_ptr = zparam;	
	}
	
	if (our_stream) {
		php_stream_close(stream);
	}	
	
	return TRUE;
}
/* }}} */

/* {{{ static svm_node* php_svm_get_data_from_array(zval *arr)
Take an array of training data and turn it into an array of svm nodes.
*/
static struct svm_node* php_svm_get_data_from_array(zval* arr) 
{
	struct svm_node *x;
	HashTable *arr_hash;
	int array_count, i;
	char *endptr;
	zval temp;
	zend_string *key;
	zend_ulong num_key;
	zval *val;
	
	arr_hash = Z_ARRVAL_P(arr);
	array_count = zend_hash_num_elements(arr_hash);
	
	/* need 1 extra to indicate the end */
	x = safe_emalloc((array_count + 1), sizeof(struct svm_node), 0);
	i = 0;
	
	/* Loop over the array in the argument and convert into svm_nodes for the prediction */
	ZEND_HASH_FOREACH_KEY_VAL(arr_hash, num_key, key, val) 
	{
		if (key) {
			x[i].index = (int) strtol(ZSTR_VAL(key), &endptr, 10);
		} else {
			x[i].index = (int) num_key;
		} 
		temp = *val;
		zval_copy_ctor(&temp);
		convert_to_double(&temp);
		x[i].value = Z_DVAL(temp);
		zval_dtor(&temp);
		i++;
	} ZEND_HASH_FOREACH_END();

	/* needed so the predictor knows when to end */
	x[i].index = -1;
	
	return x;
}
/* }}} */

/* ---- END HELPER FUNCS ---- */


/* ---- START SVM ---- */

/* {{{ SVM SVM::__construct();
The constructor
*/
PHP_METHOD(svm, __construct)
{
	php_svm_object *intern;
	zend_object * zobj;

	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "") == FAILURE) {
        SVM_THROW("Invalid parameters passed to constructor", 154);
	}
	
	//intern = (php_svm_object *) Z_OBJ_P(getThis());
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_object *)((char *)zobj - XtOffsetOf(php_svm_object, zo));
	
	/* Setup the default parameters to match those in libsvm's svm_train */
	php_svm_set_long_attribute(intern, phpsvm_svm_type, C_SVC);
	php_svm_set_long_attribute(intern, phpsvm_kernel_type, RBF);
	php_svm_set_long_attribute(intern, phpsvm_degree, 3);
	php_svm_set_double_attribute(intern, phpsvm_gamma, 0);
	php_svm_set_double_attribute(intern, phpsvm_coef0, 0);
	php_svm_set_double_attribute(intern, phpsvm_nu, 0.5);
	php_svm_set_double_attribute(intern, phpsvm_cache_size, 100.0);
	php_svm_set_double_attribute(intern, phpsvm_C, 1);
	php_svm_set_double_attribute(intern, phpsvm_eps, 1e-3);
	php_svm_set_double_attribute(intern, phpsvm_p, 0.1);
	php_svm_set_bool_attribute(intern, phpsvm_shrinking, TRUE);
	php_svm_set_bool_attribute(intern, phpsvm_probability, FALSE);
	return;
}
/* }}} */

/* {{{ array SVM::getOptions();
Get training parameters, in an array. 
*/
PHP_METHOD(svm, getOptions) 
{
	php_svm_object *intern;
	zend_object * zobj;

	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_object *)((char *)zobj - XtOffsetOf(php_svm_object, zo));
	
	array_init(return_value); 
	
	add_index_long(return_value, phpsvm_svm_type, intern->param.svm_type);
	add_index_long(return_value, phpsvm_kernel_type, intern->param.kernel_type);
	add_index_long(return_value, phpsvm_degree, intern->param.degree);
	add_index_long(return_value, phpsvm_coef0, intern->param.shrinking);
	add_index_long(return_value, phpsvm_probability, intern->param.probability == 1 ? TRUE : FALSE);
	add_index_long(return_value, phpsvm_shrinking, intern->param.shrinking == 1 ? TRUE : FALSE);
	
	add_index_double(return_value,  phpsvm_gamma, intern->param.gamma);
	add_index_double(return_value,  phpsvm_coef0, intern->param.coef0);
	add_index_double(return_value,  phpsvm_nu, intern->param.nu);
	add_index_double(return_value,  phpsvm_cache_size, intern->param.cache_size);
	add_index_double(return_value,  phpsvm_C, intern->param.C);
	add_index_double(return_value,  phpsvm_eps, intern->param.eps);
	add_index_double(return_value,  phpsvm_p, intern->param.p);
}
/* }}} */


/* {{{ int SVM::setOptopms(array params);
Takes an array of parameters and sets the training options to match them. 
Only used by the training functions, will not modify an existing model. 
*/
PHP_METHOD(svm, setOptions) 
{
	HashTable *params_ht;
	php_svm_object *intern;
	zval *params, *pzval;
	zend_string *string_key = NULL;
	zend_ulong num_key;
	zend_object * zobj;
	zend_bool boolTmp;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "a", &params) == FAILURE) {
		RETURN_FALSE;
	}
	
	params_ht = HASH_OF(params);
	
	if (zend_hash_num_elements(params_ht) == 0) {
		return;
	}
	
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_object *)((char *)zobj - XtOffsetOf(php_svm_object, zo));
	
	for (zend_hash_internal_pointer_reset(params_ht);
		 (pzval = zend_hash_get_current_data(params_ht)) != NULL;
		 zend_hash_move_forward(params_ht)) {
		
		zval tmp_zval, *tmp_pzval;
		
		if (zend_hash_get_current_key(params_ht, &string_key, &num_key) != HASH_KEY_IS_LONG) {
			continue; /* Ignore the arg (TODO: throw exception?) */
		}
		
		/* Make sure we don't modify the original array */
		tmp_zval = *pzval;
		zval_copy_ctor(&tmp_zval);
		tmp_pzval = &tmp_zval;
		
		/* Long attribute */
		if (num_key > SvmLongAttributeMin && num_key < SvmLongAttributeMax) {
	
			if (Z_TYPE_P(tmp_pzval) != IS_LONG) {
				convert_to_long(tmp_pzval);
			}
			
			if (!php_svm_set_long_attribute(intern, num_key, Z_LVAL_P(tmp_pzval))) {
				SVM_THROW("Failed to set the attribute", 999);
			}
	
		/* Double attribute */
		} else if (num_key > SvmDoubleAttributeMin && num_key < SvmDoubleAttributeMax) {
			
			if (Z_TYPE_P(tmp_pzval) != IS_DOUBLE) {
				convert_to_double(tmp_pzval);
			}
			
			if (!php_svm_set_double_attribute(intern, num_key, Z_DVAL_P(tmp_pzval))) {
				SVM_THROW("Failed to set the attribute", 999);
			}
		/* Bool attribute */
		} else if(num_key > SvmBoolAttributeMin && num_key < SvmBoolAttributeMax) {
			
			if ((Z_TYPE_P(tmp_pzval) != IS_TRUE) && (Z_TYPE_P(tmp_pzval) != IS_FALSE)) {
				convert_to_boolean(tmp_pzval);
			}

			boolTmp = FALSE;
			if(Z_TYPE_P(tmp_pzval) == IS_TRUE) {
				boolTmp = TRUE;
			}
			
			if (!php_svm_set_bool_attribute(intern, num_key, boolTmp)) {
				SVM_THROW("Failed to set the attribute", 999);
			}
		} else {
			continue; /* Ignore the arg (TODO: throw exception?) */
		}
		
		tmp_pzval = NULL;
	}
	
	RETURN_TRUE;
}
/* }}} */

/* {{{ double SVM::crossvalidate(mixed string|resource|array, long folds);
Cross validate a the SVM parameters on the training data for tuning parameters. Will attempt to train then classify 
on different segments of the training data (the total number of segments is the folds parameter). The training data
can be supplied as with the train function. For SVM classification, this will we return the correct percentage,
for regression the mean squared error. 
@throws SVMException if the data format is incorrect
*/
PHP_METHOD(svm, crossvalidate)
{
	int i;
	int total_correct = 0;
	long nrfolds;
	double total_error = 0;
	double sumv = 0, sumy = 0, sumvv = 0, sumyy = 0, sumvy = 0;
	struct svm_problem *problem;
	double returnval = 0.0;
	double *target;
	php_svm_object *intern;
	php_svm_model_object *intern_return = NULL;
	zval *zparam, data;
	zend_object * zobj;
	zval * data_p = &data;

	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "zl", &zparam, &nrfolds) == FAILURE) {
		return;
	}

	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_object *)((char *)zobj - XtOffsetOf(php_svm_object, zo));
	
	array_init(data_p);
	int ret = php_svm_get_data_from_param(intern, zparam, &data_p);
	if(ret != TRUE) {
		SVM_THROW_LAST_ERROR("Could not load data", 234);
	}

   	intern->param.nr_weight = 0;
	
	problem = php_svm_read_array(intern, &intern_return, data_p, return_value);
	if(!problem) {
		SVM_THROW_LAST_ERROR("Cross validation failed", 1001);
	}
	
 	target = emalloc(problem->l * sizeof(double));
	svm_cross_validation(problem, &(intern->param), nrfolds, target);
	if(intern->param.svm_type == EPSILON_SVR || intern->param.svm_type == NU_SVR) {
		for(i=0;i<problem->l;i++) {
			double y = problem->y[i];
			double v = target[i];
			total_error += (v-y)*(v-y);
			sumv += v;
			sumy += y;
			sumvv += v*v;
			sumyy += y*y;
			sumvy += v*y;
		}
		returnval = (total_error/problem->l); // return total_error divded by number of examples
	} else {
		for(i=0; i<problem->l; i++) {
			if(target[i] == problem->y[i]) {
				++total_correct;
			}
		}
		returnval = 1.0*total_correct/problem->l;
	}
	
	if (data_p != zparam) {
		zval_dtor(data_p);
	}
	efree(target);
	php_svm_free_problem(problem);
	
	RETURN_DOUBLE(returnval);
}
/* }}} */

/* {{{ SVMModel SVM::train(mixed string|resource|array, [array classWeights]);
Train a SVM based on the SVMLight format data either in a file, an array, or in a previously opened stream. 
@throws SVMException if the data format is incorrect. Can optionally accept a set of weights that will 
be used to multiply C. Only useful for C_SVC kernels. These should be in the form array(class (int) => weight (float)) 
*/
PHP_METHOD(svm, train)
{
	php_svm_object *intern;
	php_svm_model_object *intern_return = NULL;
	struct svm_problem *problem;
	zval data;
	zval *zparam;
	zval *weights;
	zval *pzval;
	HashTable *weights_ht;
	zend_object *zobj;
	int i;
	zend_string *key;
	zend_ulong index;
	zval * data_p = &data;

	zend_bool status = 0;
	weights = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "z|a!", &zparam, &weights) == FAILURE) {
		return;
	}

	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_object *)((char *)zobj - XtOffsetOf(php_svm_object, zo));

	if(weights && intern->param.svm_type != C_SVC) {
		SVM_THROW("Weights can only be supplied for C_SyVC training", 424);
	}
	array_init(data_p);
	int ret = php_svm_get_data_from_param(intern, zparam, &data_p);
	if(ret != TRUE) {
		zval_dtor(data_p);
		SVM_THROW_LAST_ERROR("Could not load data", 234);
	}
	
	if(weights) {
		weights_ht = Z_ARRVAL_P(weights);
		if(zend_hash_num_elements(weights_ht) > 0) {
			intern->param.nr_weight = zend_hash_num_elements(weights_ht);
			intern->param.weight_label = emalloc(intern->param.nr_weight * sizeof(int));
			intern->param.weight = emalloc(intern->param.nr_weight * sizeof(double));
		
			for (zend_hash_internal_pointer_reset(weights_ht), i = 0;
				 (pzval = zend_hash_get_current_data(weights_ht)) != NULL;
				 zend_hash_move_forward(weights_ht), i++) {

				zval tmp_zval, *tmp_pzval;

				if (zend_hash_get_current_key(weights_ht, &key, &index) == HASH_KEY_IS_LONG) {
					intern->param.weight_label[i] = (int)index;	
				
					/* Make sure we don't modify the original array */
					tmp_zval = *pzval;
					zval_copy_ctor(&tmp_zval);
					tmp_pzval = &tmp_zval;
					convert_to_double(tmp_pzval);
					intern->param.weight[i] = Z_DVAL_P(tmp_pzval);
					tmp_pzval = NULL;
				}
			}
		}
	} else {
        intern->param.nr_weight = 0;
	}

	problem = php_svm_read_array(intern, &intern_return, data_p, return_value);


	if(problem != NULL) {
		if (php_svm_train(intern, intern_return, problem)) {
			status = 1;
		} 
		php_svm_free_problem(problem);
	}

	if(weights) {
		efree(intern->param.weight_label);
		efree(intern->param.weight);
	}
	
	zval_dtor(&data);
	
	
	if (!status) {
		SVM_THROW_LAST_ERROR("Training failed", 1000);
	}
	return;
}
/* }}} */

/* ---- END SVM ---- */

/* ---- START SVMMODEL ---- */

/** {{{ SvmModel::__construct([string filename])
	Constructs an svm model
*/ 
PHP_METHOD(svmmodel, __construct)
{
	php_svm_model_object *intern;
	char *filename = NULL;
	size_t filename_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "|s!", &filename, &filename_len) == FAILURE) {
		SVM_THROW("Invalid parameters passed to constructor", 154);
	}
	
	if (!filename) {
		return;
	}

	intern = (php_svm_model_object *)Z_OBJ_P(getThis());
	intern->model = svm_load_model(filename);
	
	if (!intern->model) {
		SVM_THROW("Failed to load the model", 1233);	
	}
	
	return;
}
/* }}} */

/** {{{ SvmModel::load(string filename)
	Loads the svm model from a file
*/
PHP_METHOD(svmmodel, load)
{
	php_svm_model_object *intern;
	char *filename = NULL;
	size_t filename_len;
	zend_object * zobj;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &filename, &filename_len) == FAILURE) {
		return;
	}

	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	intern->model = svm_load_model(filename);
	
	if (!intern->model) {
		SVM_THROW("Failed to load the model", 1233);
	}
	
	RETURN_TRUE;
}
/* }}} */

/** {{{ SvmModel::save(string filename)
	Saves the svm model to a file
*/
PHP_METHOD(svmmodel, save)
{
	php_svm_model_object *intern;
	char *filename;
	zend_object *zobj;
	size_t filename_len;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &filename, &filename_len) == FAILURE) {
		return;
	}

	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	
	if (!intern->model) {
		SVM_THROW("The object does not contain a model", 2321);
	}
	
	if (svm_save_model(filename, intern->model) != 0) {
		SVM_THROW("Failed to save the model", 121);
	}
		
	RETURN_TRUE;
}
/* }}} */


/** {{{ SvmModel::getSvmType()
	Gets the type of SVM the model was trained with
*/
PHP_METHOD(svmmodel, getSvmType)
{
	php_svm_model_object *intern;
	int svm_type;
	zend_object *zobj;
	
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	if(!intern->model) {
		SVM_THROW("No model available", 106);
	}
	
	svm_type = svm_get_svm_type(intern->model);
	
	RETURN_LONG(svm_type);
}
/* }}} */



/** {{{ SvmModel::getNrClass()
	Gets the number of classes the model was trained with. Note that for a regression
	or 1 class model 2 is returned.
*/
PHP_METHOD(svmmodel, getNrClass)
{
	php_svm_model_object *intern;
	int nr_classes;
	zend_object * zobj;
	
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	if(!intern->model) {
		SVM_THROW("No model available", 106);
	}
	
	nr_classes = svm_get_nr_class(intern->model);
	
	RETURN_LONG(nr_classes);
}
/* }}} */


/** {{{ SvmModel::getLabels()
	Gets an array of labels that the model was trained with. For regression
	and one class models, an empty array is returned.
*/
PHP_METHOD(svmmodel, getLabels)
{
	php_svm_model_object *intern;
	int nr_classes;
	int* labels;
	int i;
	zend_object * zobj;
	
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	if(!intern->model) {
		SVM_THROW("No model available", 106);
	}
	
	nr_classes = svm_get_nr_class(intern->model);
	labels = safe_emalloc(nr_classes, sizeof(int), 0);
	svm_get_labels(intern->model, labels);
	
	array_init(return_value);
	
	for( i = 0; i < nr_classes; i++ ) {
		add_next_index_long(return_value, labels[i]);
	}
	
	efree(labels);
}
/* }}} */


/** {{{ SvmModel::checkProbabilityModel()
	Returns true if the model contains probability estimates
*/
PHP_METHOD(svmmodel, checkProbabilityModel)
{
	php_svm_model_object *intern;
	zend_object * zobj;
	int prob;
	
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	if(!intern->model) {
		SVM_THROW("No model available", 106);
	}
	
	prob = svm_check_probability_model(intern->model);
	
	RETURN_BOOL( prob );
}
/* }}} */


/** {{{ SvmModel::getSvrProbability()
	For regression models, returns a sigma value. If there is no probability
	information or the model is not SVR, 0 is returned.
*/
PHP_METHOD(svmmodel, getSvrProbability)
{
	php_svm_model_object *intern;
	double svr_prob;
	zend_object * zobj;
	
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	if(!intern->model) {
		SVM_THROW("No model available", 106);
	}
	
	svr_prob = svm_get_svr_probability(intern->model);
	
	RETURN_DOUBLE(svr_prob);
}
/* }}} */

/** {{{ SvmModel::predict(array data)
	Predicts based on the model
*/
PHP_METHOD(svmmodel, predict)
{
	php_svm_model_object *intern;
	double predict_label;
	struct svm_node *x;
	zval *arr;
	zend_object * zobj;

	/* we want an array of data to be passed in */
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "a", &arr) == FAILURE) {
	    return;
	}
	
	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	if(!intern->model) {
		SVM_THROW("No model available to classify with", 106);
	}
	
	x = php_svm_get_data_from_array(arr);
	predict_label = svm_predict(intern->model, x);
	efree(x);
	
	RETURN_DOUBLE(predict_label);
}

/* }}} */

/** {{{ SvmModel::predict_probability(array data, array probabilities)
	Predicts based on the model
*/
PHP_METHOD(svmmodel, predict_probability)
{
	php_svm_model_object *intern;
	double predict_probability;
	int nr_classes, i;
	double *estimates;
	struct svm_node *x;
	int max_nr_attr = 64;
	int *labels;
	zval *arr; 
	zval *retarr = NULL;
	zend_object * zobj;
	
	/* we want an array of data to be passed in */
	if (zend_parse_parameters(ZEND_NUM_ARGS(), "az/", &arr, &retarr) == FAILURE) {
	    return;
	}

	zobj = Z_OBJ_P(getThis());
	intern = (php_svm_model_object *)((char *)zobj - XtOffsetOf(php_svm_model_object, zo));
	if(!intern->model) {
		SVM_THROW("No model available to classify with", 106);
	}

	x = php_svm_get_data_from_array(arr);
	nr_classes = svm_get_nr_class(intern->model);
	estimates = safe_emalloc(nr_classes, sizeof(double), 0);
	labels = safe_emalloc(nr_classes, sizeof(int), 0);
	predict_probability = svm_predict_probability(intern->model, x, estimates);
	
	if (retarr != NULL) {
		zval_dtor(retarr);
		array_init(retarr);
		svm_get_labels(intern->model, labels);
		for (i = 0; i < nr_classes; ++i) {
			add_index_double(retarr, labels[i], estimates[i]);
		}
	}
	
	efree(estimates);
	efree(labels);
	efree(x);
	
	RETURN_DOUBLE(predict_probability);
}
/* }}} */

/* ---- END SVMMODEL ---- */

static void php_svm_object_free_storage(zend_object *object)/*{{{*/
{
	php_svm_object *intern;

	intern = (php_svm_object *)((char *)object - XtOffsetOf(php_svm_object, zo));

	if (!intern) {
		return;
	}

	zend_object_std_dtor(&intern->zo);
}/*}}}*/

static zend_object* php_svm_object_new_ex(zend_class_entry *class_type, php_svm_object **ptr)/*{{{*/
{
	php_svm_object *intern;

	/* Allocate memory for the internal structure */
	intern = (php_svm_object *) ecalloc(1, sizeof(php_svm_object) + zend_object_properties_size(class_type));

	if (ptr) {
		*ptr = intern;
	}
	
	/* Null model by default */
	memset(intern->last_error, 0, 512);

	zend_object_std_init(&intern->zo, class_type);
	object_properties_init(&intern->zo, class_type);
    intern->zo.handlers = &svm_object_handlers;

    return &intern->zo;
}/*}}}*/

static zend_object * php_svm_object_new(zend_class_entry *class_type)/*{{{*/
{
	return php_svm_object_new_ex(class_type, NULL);
}/*}}}*/

static void php_svm_model_object_free_storage(zend_object *object)/*{{{*/
{
	php_svm_model_object *intern;

	intern = (php_svm_model_object *)((char *)object - XtOffsetOf(php_svm_model_object, zo));

	if (!intern) {
		return;
	}
	
	if (intern->model) {
		#if LIBSVM_VERSION >= 300
			svm_free_and_destroy_model(&intern->model);
		#else
			svm_destroy_model(intern->model);
		#endif
	    
	    efree(intern->model);
		intern->model = NULL;
	}	
	
	if (intern->x_space) {
		efree(intern->x_space);
		intern->x_space = NULL;
	}

	zend_object_std_dtor(&intern->zo);
}/*}}}*/


static zend_object * php_svm_model_object_new_ex(zend_class_entry *class_type, php_svm_model_object **ptr)/*{{{*/
{
	//zend_object_value retval;
	php_svm_model_object *intern;

	/* Allocate memory for the internal structure */
	intern = (php_svm_model_object *) ecalloc(1, sizeof(php_svm_model_object)  + zend_object_properties_size(class_type));

	if (ptr) {
		*ptr = intern;
	}

	intern->x_space = emalloc(problemSize * sizeof(struct svm_node));
	intern->model = NULL;
	
	zend_object_std_init(&intern->zo, class_type);

	object_properties_init(&intern->zo, class_type);
 
    intern->zo.handlers = &svm_model_object_handlers;

    return &intern->zo;
}/*}}}*/


static zend_object * php_svm_model_object_new(zend_class_entry *class_type)/*{{{*/
{
	return php_svm_model_object_new_ex(class_type, NULL);
}/*}}}*/

/* {{{ SVM arginfo */
ZEND_BEGIN_ARG_INFO_EX(svm_empty_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_train_args, 0, 0, 1)
	ZEND_ARG_INFO(0, problem)
	ZEND_ARG_INFO(0, weights)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_crossvalidate_args, 0, 0, 2)
	ZEND_ARG_INFO(0, problem)
	ZEND_ARG_INFO(0, number_of_folds)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_params_args, 0, 0, 1)
	ZEND_ARG_INFO(0, params)
ZEND_END_ARG_INFO()
/* }}} */

static zend_function_entry php_svm_class_methods[] =/*{{{*/
{
	PHP_ME(svm, __construct,	svm_empty_args,	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(svm, getOptions,		svm_empty_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, setOptions,		svm_params_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, train,			svm_train_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, crossvalidate,	svm_crossvalidate_args,	ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};/*}}}*/

/* {{{ Model arginfo */
ZEND_BEGIN_ARG_INFO_EX(svm_model_construct_args, 0, 0, 0)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_predict_args, 0, 0, 1)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_predict_probs_args, 0, 0, 1)
	ZEND_ARG_INFO(0, data)
	ZEND_ARG_INFO(1, probabilities)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_file_args, 0, 0, 1)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_info_args, 0, 0, 0)
ZEND_END_ARG_INFO()
/* }}} */

static zend_function_entry php_svm_model_class_methods[] =/*{{{*/
{
	PHP_ME(svmmodel, __construct,	svm_model_construct_args,	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(svmmodel, save,			svm_model_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, load,			svm_model_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, getSvmType,	svm_model_info_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, getLabels,		svm_model_info_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, getNrClass,	svm_model_info_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, getSvrProbability,	svm_model_info_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, checkProbabilityModel,	svm_model_info_args,	ZEND_ACC_PUBLIC)	
	PHP_ME(svmmodel, predict, 		svm_model_predict_args, ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, predict_probability,	svm_model_predict_probs_args, ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};/*}}}*/

PHP_MINIT_FUNCTION(svm)/*{{{*/
{
	zend_class_entry ce;
	memcpy(&svm_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	svm_object_handlers.free_obj = php_svm_object_free_storage;
	svm_object_handlers.offset = XtOffsetOf(php_svm_object, zo);

	memcpy(&svm_model_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	svm_model_object_handlers.free_obj = php_svm_model_object_free_storage;
	svm_model_object_handlers.offset   = XtOffsetOf(php_svm_model_object, zo);

	INIT_CLASS_ENTRY(ce, "svm", php_svm_class_methods);
	ce.create_object = php_svm_object_new;
	php_svm_sc_entry = zend_register_internal_class(&ce);

	INIT_CLASS_ENTRY(ce, "svmmodel", php_svm_model_class_methods);
	ce.create_object = php_svm_model_object_new;
	php_svm_model_sc_entry = zend_register_internal_class(&ce);

	INIT_CLASS_ENTRY(ce, "svmexception", NULL);
	php_svm_exception_sc_entry = zend_register_internal_class_ex(&ce, zend_exception_get_default());
	php_svm_exception_sc_entry->ce_flags |= ZEND_ACC_FINAL;
	
	/* Redirect the lib svm output */
	#if LIBSVM_VERSION >= 291
	svm_set_print_string_function(&print_null);
	#else
	svm_print_string = &print_null;
	#endif

        #define SVM_REGISTER_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(php_svm_sc_entry, const_name, sizeof(const_name)-1, value);	

	/* SVM types */
	SVM_REGISTER_CONST_LONG("C_SVC", C_SVC);
	SVM_REGISTER_CONST_LONG("NU_SVC", NU_SVC);
	SVM_REGISTER_CONST_LONG("ONE_CLASS", ONE_CLASS);
	SVM_REGISTER_CONST_LONG("EPSILON_SVR", EPSILON_SVR);
	SVM_REGISTER_CONST_LONG("NU_SVR", NU_SVR);
	
	/* Kernel types */
	SVM_REGISTER_CONST_LONG("KERNEL_LINEAR", LINEAR);
	SVM_REGISTER_CONST_LONG("KERNEL_POLY", POLY);
	SVM_REGISTER_CONST_LONG("KERNEL_RBF", RBF);
	SVM_REGISTER_CONST_LONG("KERNEL_SIGMOID", SIGMOID);
	SVM_REGISTER_CONST_LONG("KERNEL_PRECOMPUTED", PRECOMPUTED);
	
	/* Long options (for setOptions) */
	SVM_REGISTER_CONST_LONG("OPT_TYPE", phpsvm_svm_type);
	SVM_REGISTER_CONST_LONG("OPT_KERNEL_TYPE", phpsvm_kernel_type);
	SVM_REGISTER_CONST_LONG("OPT_DEGREE", phpsvm_degree);
	SVM_REGISTER_CONST_LONG("OPT_SHRINKING", phpsvm_shrinking);
	SVM_REGISTER_CONST_LONG("OPT_PROBABILITY", phpsvm_probability);
	
	/* Double options (for setOptions) */
	SVM_REGISTER_CONST_LONG("OPT_GAMMA",  phpsvm_gamma);
	SVM_REGISTER_CONST_LONG("OPT_NU", phpsvm_nu);
	SVM_REGISTER_CONST_LONG("OPT_EPS", phpsvm_eps);
	SVM_REGISTER_CONST_LONG("OPT_P", phpsvm_p);
	SVM_REGISTER_CONST_LONG("OPT_COEF_ZERO", phpsvm_coef0);
	SVM_REGISTER_CONST_LONG("OPT_C", phpsvm_C);
	SVM_REGISTER_CONST_LONG("OPT_CACHE_SIZE", phpsvm_cache_size);

#undef SVM_REGISTER_CONST_LONG

	return SUCCESS;
}/*}}}*/

PHP_MSHUTDOWN_FUNCTION(svm)/*{{{*/
{
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}/*}}}*/

PHP_MINFO_FUNCTION(svm)/*{{{*/
{
	php_info_print_table_start();
		php_info_print_table_header(2, "svm extension", "enabled");
		php_info_print_table_row(2, "svm extension version", PHP_SVM_EXTVER);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}/*}}}*/

/* No global functions */
zend_function_entry svm_functions[] = {
	{NULL, NULL, NULL} 
};

zend_module_entry svm_module_entry =
{
	STANDARD_MODULE_HEADER,
	PHP_SVM_EXTNAME,
	svm_functions,				/* Functions */
	PHP_MINIT(svm),				/* MINIT */
	PHP_MSHUTDOWN(svm),			/* MSHUTDOWN */
	NULL,						/* RINIT */
	NULL,						/* RSHUTDOWN */
	PHP_MINFO(svm),				/* MINFO */
	PHP_SVM_EXTVER,				/* version */
	STANDARD_MODULE_PROPERTIES
};


#ifdef COMPILE_DL_SVM
ZEND_GET_MODULE(svm)
#endif /* COMPILE_DL_SVM */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
