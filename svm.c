/*
  +----------------------------------------------------------------------+
  | PHP Version 5 / svm                                                  |
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
#include "php_ini.h" /* needed for 5.2 */
#include "Zend/zend_exceptions.h"
#include "ext/standard/info.h"

zend_class_entry *php_svm_sc_entry;
zend_class_entry *php_svm_model_sc_entry;
zend_class_entry *php_svm_exception_sc_entry;

static zend_object_handlers svm_object_handlers;
static zend_object_handlers svm_model_object_handlers;

#define SVM_MAX_LINE_SIZE 4096
#define SVM_THROW(message, code) \
			zend_throw_exception(php_svm_exception_sc_entry, message, (long)code TSRMLS_CC); \
			return;

ZEND_DECLARE_MODULE_GLOBALS(svm);

#define SVM_SET_ERROR_MSG(intern, ...) snprintf(intern->last_error, 512, __VA_ARGS__);

/* 
 TODO: Change train array format 
 TODO: Retrieve parameters for existing model (is this worth doing?)
 TODO: Accept training data in an array
 TODO: Test multilabel
 TODO: Cross validation
 TODO: Add serialize and wake up support
 TODO: Probability support
 TODO: Validate the data passed to train, to avoid it crashing
 TODO: Support stream context setting
 TODO: LibSVM+ support
 TODO: Catch the printed data from libsvm via print_null and store for logging
 TODO: Add tests for different kernel parameters
 TODO: Kernel and SVM type validation
 TODO: Support weight label and weight in params
*/

void print_null(const char *s) {}

typedef enum SvmLongAttribute {
	SvmLongAttributeMin = 100,
	phpsvm_svm_type,
	phpsvm_kernel_type,
	phpsvm_degree,
	phpsvm_shrinking,
	phpsvm_probability,
	phpsvm_nr_weight,
	phpsvm_weight_label,
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
	phpsvm_weight,
	SvmDoubleAttributeMax /* Always add before this */
} SvmDoubleAttribute;

static zend_bool php_svm_set_double_attribute(php_svm_object *intern, SvmDoubleAttribute name, double value) 
{
	if (name >= SvmDoubleAttributeMax) {
		return 0;
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
		case phpsvm_weight:
			/* Pointer */
			intern->param.weight = &value;
			break;
		default:
			return 0;
	}
	
	return 1;
}

static zend_bool php_svm_set_long_attribute(php_svm_object *intern, SvmLongAttribute name, long value) 
{
	if (name >= SvmLongAttributeMax) {
		return 0;
	}

	switch (name) {
		case phpsvm_svm_type:
			/* TODO: Validation against list */
			intern->param.svm_type = (int)value;
			break;
		case phpsvm_kernel_type:
			/* TODO: Validation against list */
			intern->param.kernel_type = (int)value;
			break;
		case phpsvm_degree:
			intern->param.degree = (int)value;
			break;
		case phpsvm_shrinking:
			intern->param.shrinking = value;
			break;
		case phpsvm_probability:
			intern->param.probability = value;
			break;
		case phpsvm_nr_weight:
			intern->param.nr_weight = value;
			break;
		case phpsvm_weight_label:
			intern->param.weight_label = &value; /* TODO: should be array of ints */
			break;
		default:
			return 0;
	}
	
	return 1;
}

/* {{{ SVM SVM::__construct();
The constructor
*/
PHP_METHOD(svm, __construct)
{
	php_svm_object *intern;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "") == FAILURE) {
		return;
	}
	
	intern = (php_svm_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
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
	php_svm_set_long_attribute(intern, phpsvm_shrinking, 1);
	php_svm_set_long_attribute(intern, phpsvm_probability, 0);
	php_svm_set_long_attribute(intern, phpsvm_nr_weight, 0);
	/* TODO: Support these param types */
	/*php_svm_set_long_attribute(intern, phpsvm_weight_label, NULL);
	php_svm_set_double_attribute(intern, phpsvm_weight, NULL); */
	
	return;
}
/* }}} */

/* {{{ array SVM::getOptions();
Get training parameters, in an array. 
*/
PHP_METHOD(svm, getOptions) 
{
	php_svm_object *intern;
	intern = zend_object_store_get_object(getThis() TSRMLS_CC);
	
	array_init(return_value); 
	
	/* TODO: Make this not suck, this is deeply useless. At the moment we can only
	see the parameters based on looking themn up in the array with the SVM constants.
	May be worth keeping this and adding friendly getters setters.  */
	add_index_long(return_value, phpsvm_svm_type, intern->param.svm_type);
	add_index_long(return_value, phpsvm_kernel_type, intern->param.kernel_type);
	add_index_long(return_value, phpsvm_degree, intern->param.degree);
	add_index_long(return_value, phpsvm_coef0, intern->param.shrinking);
	add_index_long(return_value, phpsvm_probability, intern->param.probability);
	add_index_long(return_value, phpsvm_nr_weight, intern->param.nr_weight);
	
	add_index_long(return_value,  phpsvm_gamma, intern->param.gamma);
	add_index_long(return_value,  phpsvm_coef0, intern->param.coef0);
	add_index_long(return_value,  phpsvm_nu, intern->param.nu);
	add_index_long(return_value,  phpsvm_cache_size, intern->param.cache_size);
	add_index_long(return_value,  phpsvm_C, intern->param.C);
	add_index_long(return_value,  phpsvm_eps, intern->param.eps);
	add_index_long(return_value,  phpsvm_p, intern->param.p);
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
	zval *params, **ppzval;
	char *string_key = NULL;
	ulong num_key;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &params) == FAILURE) {
		return;
	}
	
	params_ht = HASH_OF(params);
	
	if (zend_hash_num_elements(params_ht) == 0) {
		return;
	}
	
	intern = (php_svm_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	for (zend_hash_internal_pointer_reset(params_ht);
		 zend_hash_get_current_data(params_ht, (void **) &ppzval) == SUCCESS;
		 zend_hash_move_forward(params_ht)) {
		
		zval tmp_zval, *tmp_pzval;
		
		if (zend_hash_get_current_key(params_ht, &string_key, &num_key, 1) != HASH_KEY_IS_LONG) {
			continue; /* Ignore the arg (TODO: throw exception?) */
		}
		
		/* Make sure we don't modify the original array */
		tmp_zval = **ppzval;
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
			
		} else {
			continue; /* Ignore the arg (TODO: throw exception?) */
		}
		
		tmp_pzval = NULL;
	}
	
	RETURN_TRUE;
}
/* }}} */

/* ---- START SVMMODEL ---- */

/** {{{ SvmModel::__construct([string filename])
	Constructs an svm model
*/ 
PHP_METHOD(svmmodel, __construct)
{
	php_svm_model_object *intern;
	char *filename = NULL;
	int filename_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "|s!", &filename, &filename_len) == FAILURE) {
		return;
	}
	
	if (!filename)
		return;

	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->model = svm_load_model(filename);
	
	/* TODO: Probability support
			if(svm_check_probability_model(model)==0)
			if(svm_check_probability_model(model)!=0)
			*/
	
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
	int filename_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &filename, &filename_len) == FAILURE) {
		return;
	}

	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	intern->model = svm_load_model(filename);
	
	/* TODO: Probability support
			if(svm_check_probability_model(model)==0)
			if(svm_check_probability_model(model)!=0)
			*/
	
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
	int filename_len;
	
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s", &filename, &filename_len) == FAILURE) {
		return;
	}
	
	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	
	if (!intern->model) {
		SVM_THROW("The object does not contain a model", 2321);
	}
	
	if (svm_save_model(filename, intern->model) != 0) {
		SVM_THROW("Failed to save the model", 121);
	}
		
	RETURN_TRUE;
}
/* }}} */

/** {{{ SvmModel::predict(array data)
	Predicts based on the model
*/
PHP_METHOD(svmmodel, predict)
{
	/* TODO: probability stuff */
	php_svm_model_object *intern;
	double predict_label;
	struct svm_node *x;
	int max_nr_attr = 64;
	zval *arr, **data;
	HashTable *arr_hash;
	HashPosition pointer;
	int array_count, i;
	char *endptr;

	/* we want an array of data to be passed in */
	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a", &arr) == FAILURE) {
	    return;
	}

	arr_hash = Z_ARRVAL_P(arr);
	array_count = zend_hash_num_elements(arr_hash);
	intern = (php_svm_model_object *)zend_object_store_get_object(getThis() TSRMLS_CC);
	if(!intern->model) {
		SVM_THROW("No model available to classify with", 106);
	}
	
	/* need 1 extra to indicate the end */
	x = emalloc((array_count + 1) *sizeof(struct svm_node));
	
	i = 0;
	zval temp;
	char *key;
	uint key_len;
	ulong index;
	
	/* Loop over the array in the argument and convert into svm_nodes for the prediction */
	for(zend_hash_internal_pointer_reset_ex(arr_hash, &pointer);
		zend_hash_get_current_data_ex(arr_hash, (void**) &data, &pointer) == SUCCESS; 
		zend_hash_move_forward_ex(arr_hash, &pointer)) 
	{
		if (zend_hash_get_current_key_ex(arr_hash, &key, &key_len, &index, 0, &pointer) == HASH_KEY_IS_STRING) {
			x[i].index = (int) strtol(key, &endptr, 10);
		} else {
			x[i].index = (int) index;
		} 
		temp = **data;
		zval_copy_ctor(&temp);
		convert_to_double(&temp);
		x[i].value = Z_DVAL(temp);
		zval_dtor(&temp);
		i++;
	}
	/* needed so the predictor knows when to end */
	x[i].index = -1;

	/*
	TODO: This stuff
	if (predict_probability && (svm_type==C_SVC || svm_type==NU_SVC))
	{
		predict_label = svm_predict_probability(model,x,prob_estimates);
		fprintf(output,"%g",predict_label);
		for(j=0;j<nr_class;j++)
			fprintf(output," %g",prob_estimates[j]);
		fprintf(output,"\n");
	}
	*/
	
	predict_label = svm_predict(intern->model, x);
	efree(x);

	/*
	TODO: Prob stuff
	if(predict_probability)
		free(prob_estimates);
		*/	
	RETURN_DOUBLE(predict_label);
}
/* }}} */

/** {{{ zend_bool php_svm_stream_to_array(php_svm_object *intern, php_stream *stream, zval *retval TSRMLS_DC)
	Take a stream containing lines of SVMLight format data and convert them into a PHP array for use by the training
	function.
*/
static zend_bool php_svm_stream_to_array(php_svm_object *intern, php_stream *stream, zval *retval TSRMLS_DC)
{
	while (!php_stream_eof(stream)) {
		char buf[SVM_MAX_LINE_SIZE];
		size_t retlen = 0;
		int line = 1;
		
		/* Read line by line */
		if (php_stream_get_line(stream, buf, SVM_MAX_LINE_SIZE, &retlen)) {
		
			zval *line_array, *pz_label;
			char *label, *ptr, *l = NULL;

			ptr   = buf;
			label = php_strtok_r(ptr, " \t", &l);

			if (!label) {
				SVM_SET_ERROR_MSG(intern, "Incorrect data format on line %d", line);
				return 0;
			}
			
			/* The line array */
			MAKE_STD_ZVAL(line_array);
			array_init(line_array);

			/* The label */
			MAKE_STD_ZVAL(pz_label);
			ZVAL_STRING(pz_label, label, 1);
			convert_to_double(pz_label);

			/* Label is the first item in the line array */
			add_next_index_zval(line_array, pz_label);
			
			/* Read rest of the values on the line */
			while (1) {
				char *idx, *value;
				zval *pz_idx, *pz_value;
				
				/* idx:value format */
				idx   = php_strtok_r(NULL, ":", &l);
				value = php_strtok_r(NULL, " \t", &l);
			
				if (!value)
					break;
				
				/* Make zvals and convert to correct types */
				MAKE_STD_ZVAL(pz_idx);
				ZVAL_STRING(pz_idx, idx, 1);
				convert_to_long(pz_idx);
				
				MAKE_STD_ZVAL(pz_value);
				ZVAL_STRING(pz_value, value, 1);
				convert_to_double(pz_value);
				
				add_next_index_zval(line_array, pz_idx);
				add_next_index_zval(line_array, pz_value);
			}
			add_next_index_zval(retval, line_array);
			line++;
		}
	}
	return 1;
}
/* }}} */


/* {{{ int _php_count_values(zval *array);
For a an array of arrays, count the number of items in all subarrays. 
*/
static int _php_count_values(zval *array)
{
	zval **ppzval;
	int values = 0;

	for (zend_hash_internal_pointer_reset(Z_ARRVAL_P(array));
		 zend_hash_get_current_data(Z_ARRVAL_P(array), (void **) &ppzval) == SUCCESS;
		 zend_hash_move_forward(Z_ARRVAL_P(array))) {
		
		if (Z_TYPE_PP(ppzval) == IS_ARRAY)
			values += zend_hash_num_elements(Z_ARRVAL_PP(ppzval));
	}
	return values;
}
/* }}} */

/* {{{ static zend_bool php_svm_read_array(php_svm_object *intern, php_svm_model_object *model. zval *array TSRMLS_DC)
Take a PHP array, and prepare libSVM problem data for training with. 
*/
static zend_bool php_svm_read_array(php_svm_object *intern, php_svm_model_object *intern_model, zval *array TSRMLS_DC)
{
	zval **ppzval;
	
	const char *err_msg;
	int i, j = 0, num_labels, elements, max_index = 0, inst_max_index = 0;
	struct svm_problem *problem;
	
	/* If reading multiple times make sure that we don't leak */
	if (intern_model->x_space) {
		efree(intern_model->x_space);
		intern_model->x_space = NULL;
	}
	
	if (intern_model->model) {
		svm_destroy_model(intern_model->model);
		intern_model->model = NULL;
	}
	
	/* Allocate the problem */
	problem = emalloc(sizeof(struct svm_problem));
	
	/* x and y */
	num_labels = zend_hash_num_elements(HASH_OF(array));
	
	/* Allocate space for the labels */
	problem->y = emalloc(num_labels * sizeof(double));
	
	/* allocate space for x */
	problem->x = emalloc(num_labels * sizeof(struct svm_node *));
	
	/* total number of elements */
	elements = _php_count_values(array);
	
	intern_model->x_space = emalloc(elements * sizeof(struct svm_node));

	/* How many labels */
	problem->l = num_labels;

	/* Fill the problem */
	for (zend_hash_internal_pointer_reset(Z_ARRVAL_P(array)), i = 0;
		 zend_hash_get_current_data(Z_ARRVAL_P(array), (void **) &ppzval) == SUCCESS;
		 zend_hash_move_forward(Z_ARRVAL_P(array)), i++) {
	
		if (Z_TYPE_PP(ppzval) == IS_ARRAY) {
			zval **ppz_label;
			
			problem->x[i] = &(intern_model->x_space[j]);
			
			zend_hash_internal_pointer_reset(Z_ARRVAL_PP(ppzval));
			
			if ((zend_hash_get_current_data(Z_ARRVAL_PP(ppzval), (void **) &ppz_label) == SUCCESS) &&
			    (zend_hash_move_forward(Z_ARRVAL_PP(ppzval)) == SUCCESS)) {
				
				if (Z_TYPE_PP(ppz_label) != IS_DOUBLE) {
					convert_to_double(*ppz_label);
				}
				problem->y[i] = Z_DVAL_PP(ppz_label);
			}
			
			while (1) {
				zval **ppz_idz, **ppz_value;

				if ((zend_hash_get_current_data(Z_ARRVAL_PP(ppzval), (void **) &ppz_idz) == SUCCESS) &&
					(zend_hash_move_forward(Z_ARRVAL_PP(ppzval)) == SUCCESS) &&
					(zend_hash_get_current_data(Z_ARRVAL_PP(ppzval), (void **) &ppz_value) == SUCCESS)) {						

					if (Z_TYPE_PP(ppz_label) != IS_LONG) {
						convert_to_long(*ppz_idz);
					}
					intern_model->x_space[j].index = (int) Z_LVAL_PP(ppz_idz);
					
					if (Z_TYPE_PP(ppz_label) != IS_DOUBLE) {
						convert_to_double(*ppz_value);
					}
					intern_model->x_space[j].value = Z_DVAL_PP(ppz_value);
				
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
		}
	}
	
	if (intern->param.gamma == 0 && max_index > 0) {
		intern->param.gamma = 1.0/max_index;
	}
	
	err_msg = svm_check_parameter(problem, &(intern->param));
	
	if (err_msg) {
		SVM_SET_ERROR_MSG(intern, err_msg);
		return 0;
	}
	
	intern_model->model = svm_train(problem, &(intern->param));
	
	efree(problem->x);
	efree(problem->y);
	efree(problem);

	/* Failure ? */
	if (!intern_model->model) {
		SVM_SET_ERROR_MSG(intern, "Failed to train using the data");
		return 0;
	}
	return 1;
}

/* {{{ SVMModel SVM::train(mixed filename|handle);
Train a SVM based on the SVMLight format data either in a file, or in a previously opened stream. 
@throws SVMException if the data format is incorrect
*/
PHP_METHOD(svm, train)
{
	php_svm_object *intern;
	php_svm_model_object *intern_return;
	
	php_stream *stream = NULL;
	zval *zstream, *retval;
	zend_bool our_stream;
	
	zend_bool status = 0;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", &zstream) == FAILURE) {
		return;
	}

	intern = (php_svm_object *)zend_object_store_get_object(getThis() TSRMLS_CC);

	if (Z_TYPE_P(zstream) == IS_STRING) {
		stream = php_stream_open_wrapper(Z_STRVAL_P(zstream), "r", ENFORCE_SAFE_MODE | REPORT_ERRORS, NULL);
		our_stream = 1;
	} else if (Z_TYPE_P(zstream) == IS_RESOURCE){
		php_stream_from_zval(stream, &zstream);
		our_stream = 0;
		
		if (!stream) {
			RETURN_FALSE;
		}
	}
	
	/* Initialize as an array */
	MAKE_STD_ZVAL(retval);
	array_init(retval);

	/* Need to make an array out of the file */
	if (php_svm_stream_to_array(intern, stream, retval TSRMLS_CC)) {

		object_init_ex(return_value, php_svm_model_sc_entry);
		intern_return = (php_svm_model_object *)zend_object_store_get_object(return_value TSRMLS_CC);
		
		if (php_svm_read_array(intern, intern_return, retval TSRMLS_CC)) {
			status = 1;
		}
	}
	zval_dtor(retval);
	FREE_ZVAL(retval);
	
	if (our_stream) {
		php_stream_close(stream);
	}
	
	if (!status) {
		zval_dtor(return_value);
		SVM_THROW((strlen(intern->last_error) > 0 ? intern->last_error : "Training failed"), 1000);
	}
	
	return;
}
/* }}} */

static void php_svm_init_globals(zend_svm_globals *svm_globals)
{
	/* No globals */
}

static void php_svm_object_free_storage(void *object TSRMLS_DC)
{
	php_svm_object *intern = (php_svm_object *)object;

	if (!intern) {
		return;
	}

	zend_object_std_dtor(&intern->zo TSRMLS_CC);
	efree(intern);
}

static zend_object_value php_svm_object_new_ex(zend_class_entry *class_type, php_svm_object **ptr TSRMLS_DC)
{
	zval *tmp;
	zend_object_value retval;
	php_svm_object *intern;

	/* Allocate memory for the internal structure */
	intern = (php_svm_object *) emalloc(sizeof(php_svm_object));
	memset(&intern->zo, 0, sizeof(zend_object));

	if (ptr) {
		*ptr = intern;
	}
	
	/* Null model by default */
	memset(intern->last_error, 0, 512);

	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	zend_hash_copy(intern->zo.properties, &class_type->default_properties, (copy_ctor_func_t) zval_add_ref,(void *) &tmp, sizeof(zval *));

	retval.handle = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_svm_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = (zend_object_handlers *) &svm_object_handlers;
	return retval;
}

static zend_object_value php_svm_object_new(zend_class_entry *class_type TSRMLS_DC)
{
	return php_svm_object_new_ex(class_type, NULL TSRMLS_CC);
}

static void php_svm_model_object_free_storage(void *object TSRMLS_DC)
{
	php_svm_model_object *intern = (php_svm_model_object *)object;

	if (!intern) {
		return;
	}
	
	if (intern->model) {
		svm_destroy_model(intern->model);
		intern->model = NULL;
	}	
	
	if (intern->x_space) {
		efree(intern->x_space);
		intern->x_space = NULL;
	}

	zend_object_std_dtor(&intern->zo TSRMLS_CC);
	efree(intern);
}

static zend_object_value php_svm_model_object_new_ex(zend_class_entry *class_type, php_svm_model_object **ptr TSRMLS_DC)
{
	zval *tmp;
	zend_object_value retval;
	php_svm_model_object *intern;

	/* Allocate memory for the internal structure */
	intern = (php_svm_model_object *) emalloc(sizeof(php_svm_object));
	memset(&intern->zo, 0, sizeof(zend_object));

	if (ptr) {
		*ptr = intern;
	}
	
	/* Null model by default */
	intern->model = NULL;
	intern->x_space = NULL;
	
	zend_object_std_init(&intern->zo, class_type TSRMLS_CC);
	zend_hash_copy(intern->zo.properties, &class_type->default_properties, (copy_ctor_func_t) zval_add_ref,(void *) &tmp, sizeof(zval *));

	retval.handle = zend_objects_store_put(intern, NULL, (zend_objects_free_object_storage_t) php_svm_model_object_free_storage, NULL TSRMLS_CC);
	retval.handlers = (zend_object_handlers *) &svm_model_object_handlers;
	return retval;
}

static zend_object_value php_svm_model_object_new(zend_class_entry *class_type TSRMLS_DC)
{
	return php_svm_model_object_new_ex(class_type, NULL TSRMLS_CC);
}

ZEND_BEGIN_ARG_INFO_EX(svm_empty_args, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_train_args, 0, 0, 1)
	ZEND_ARG_INFO(0, problem)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_params_args, 0, 0, 1)
	ZEND_ARG_INFO(0, params)
ZEND_END_ARG_INFO()

static function_entry php_svm_class_methods[] =
{
	PHP_ME(svm, __construct,	svm_empty_args,	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(svm, getOptions,		svm_empty_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, setOptions,		svm_params_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svm, train,			svm_train_args,	ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

ZEND_BEGIN_ARG_INFO_EX(svm_model_construct_args, 0, 0, 0)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_predict_args, 0, 0, 1)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(svm_model_file_args, 0, 0, 1)
	ZEND_ARG_INFO(0, filename)
ZEND_END_ARG_INFO()

static function_entry php_svm_model_class_methods[] =
{
	PHP_ME(svmmodel, __construct,	svm_model_construct_args,	ZEND_ACC_PUBLIC|ZEND_ACC_CTOR)
	PHP_ME(svmmodel, save,			svm_model_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, load,			svm_model_file_args,	ZEND_ACC_PUBLIC)
	PHP_ME(svmmodel, predict, 		svm_model_predict_args, ZEND_ACC_PUBLIC)
	{ NULL, NULL, NULL }
};

PHP_MINIT_FUNCTION(svm)
{
	zend_class_entry ce;
	ZEND_INIT_MODULE_GLOBALS(svm, php_svm_init_globals, NULL);

	memcpy(&svm_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	memcpy(&svm_model_object_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));

	INIT_CLASS_ENTRY(ce, "svm", php_svm_class_methods);
	ce.create_object = php_svm_object_new;
	svm_object_handlers.clone_obj = NULL;
	php_svm_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);

	INIT_CLASS_ENTRY(ce, "svmmodel", php_svm_model_class_methods);
	ce.create_object = php_svm_model_object_new;
	svm_object_handlers.clone_obj = NULL;
	php_svm_model_sc_entry = zend_register_internal_class(&ce TSRMLS_CC);

	INIT_CLASS_ENTRY(ce, "svmexception", NULL);
	php_svm_exception_sc_entry = zend_register_internal_class_ex(&ce, zend_exception_get_default(TSRMLS_C), NULL TSRMLS_CC);
	php_svm_exception_sc_entry->ce_flags |= ZEND_ACC_FINAL;
	
	/* Redirect the lib svm output */
	extern void (*svm_print_string) (const char *);
	svm_print_string = &print_null;

#define SVM_REGISTER_CONST_LONG(const_name, value) \
	zend_declare_class_constant_long(php_svm_sc_entry, const_name, sizeof(const_name)-1, (long)value TSRMLS_CC);	

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
	SVM_REGISTER_CONST_LONG("OPT_PROPABILITY", phpsvm_probability);
	SVM_REGISTER_CONST_LONG("OPT_NR_WEIGHT", phpsvm_nr_weight);
	SVM_REGISTER_CONST_LONG("OPT_WEIGHT_LABEL", phpsvm_weight_label);
	
	/* Double options (for setOptions) */
	SVM_REGISTER_CONST_LONG("OPT_GAMMA",  phpsvm_gamma);
	SVM_REGISTER_CONST_LONG("OPT_NU", phpsvm_nu);
	SVM_REGISTER_CONST_LONG("OPT_EPS", phpsvm_eps);
	SVM_REGISTER_CONST_LONG("OPT_P", phpsvm_p);
	SVM_REGISTER_CONST_LONG("OPT_COEF_ZERO", phpsvm_coef0);
	SVM_REGISTER_CONST_LONG("OPT_C", phpsvm_C);
	SVM_REGISTER_CONST_LONG("OPT_CACHE_SIZE", phpsvm_cache_size);
	SVM_REGISTER_CONST_LONG("OPT_WEIGHT", phpsvm_weight);

#undef SVM_REGISTER_CONST_LONG

	/* Redirect the lib svm output */
	extern void (*svm_print_string) (const char *);
	svm_print_string = &print_null;

	return SUCCESS;
}

PHP_MSHUTDOWN_FUNCTION(svm)
{
	UNREGISTER_INI_ENTRIES();
	return SUCCESS;
}

PHP_MINFO_FUNCTION(svm)
{
	php_info_print_table_start();
		php_info_print_table_header(2, "svm extension", "enabled");
		php_info_print_table_row(2, "svm extension version", PHP_SVM_EXTVER);
	php_info_print_table_end();

	DISPLAY_INI_ENTRIES();
}

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
