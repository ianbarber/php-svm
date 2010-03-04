--TEST--
Test for various kernels
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$data = array(
	"1" => 1,
	2 => -0.731729,
	3 => -0.886786,
	4 => -1,
	5 => 0.230769,
	"6" => -0.25,
	7 => -0.783509,
	8 => 1,
	9 => 1, 
	10 => "-0.820896",
	11 => -1, 
	13 => -0.92,
	"14" => "-1"
);

$kernels = array(
	array(
		SVM::OPT_TYPE => SVM::C_SVC,
		SVM::OPT_KERNEL_TYPE => SVM::KERNEL_POLY,
	),
	array(
		SVM::OPT_TYPE => SVM::ONE_CLASS,
		SVM::OPT_KERNEL_TYPE => SVM::KERNEL_RBF,
	),
	array(
		SVM::OPT_TYPE => SVM::EPSILON_SVR,
		SVM::OPT_KERNEL_TYPE => SVM::KERNEL_SIGMOID,
	),
	array(
		SVM::OPT_TYPE => SVM::NU_SVR,
		SVM::OPT_KERNEL_TYPE => SVM::KERNEL_PRECOMPUTED,
	),
);

$svm = new svm();

foreach($kernels as $kernel) {
	$svm->setOptions($kernel);
	$model = $svm->train(dirname(__FILE__) . '/australian.scale');

	if($model) {
		echo "ok train " . $kernel[SVM::OPT_TYPE] . "\n";

		$result = $model->predict($data);
		if($result != false) {
			echo "ok " . $kernel[SVM::OPT_TYPE] . "\n";
		} else {
			echo "failed: " . $result . "\n";
		}
	} else {
		echo "training failed";
	}
}
?>
--EXPECT--
ok train 0
ok 0
ok train 2
ok 2
ok train 3
ok 3
ok train 4
ok 4