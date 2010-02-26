--TEST--
Test setOptions
--SKIPIF--
<?php
if (!extension_loaded('svm')) die('skip');
?>
--FILE--
<?php
$svm = new svm();

$options = array(Svm::OPT_TYPE => "111", Svm::OPT_COEF_ZERO => 1.2);

$svm->setOptions($options);

var_dump(is_string($options[Svm::OPT_TYPE]));

echo "ok";
?>
--EXPECT--
bool(true)
ok
