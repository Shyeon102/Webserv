<?php
function h($value) {
    return htmlspecialchars((string)$value, ENT_QUOTES, "UTF-8");
}

function num_param($name, $default) {
    if (!isset($_REQUEST[$name]) || $_REQUEST[$name] === '') {
        return $default;
    }
    return (float)$_REQUEST[$name];
}

function op_param() {
    if (!isset($_REQUEST['op'])) {
        return 'add';
    }
    $op = $_REQUEST['op'];
    $allowed = array('add', 'sub', 'mul', 'div', 'mod', 'pow');
    return in_array($op, $allowed, true) ? $op : 'add';
}

function op_label($op) {
    $labels = array(
        'add' => '+',
        'sub' => '-',
        'mul' => '*',
        'div' => '/',
        'mod' => '%',
        'pow' => '^'
    );
    return isset($labels[$op]) ? $labels[$op] : '+';
}

function calculate($a, $b, $op, &$error) {
    $error = '';
    if ($op === 'add') {
        return $a + $b;
    }
    if ($op === 'sub') {
        return $a - $b;
    }
    if ($op === 'mul') {
        return $a * $b;
    }
    if ($op === 'div') {
        if ($b == 0.0) {
            $error = 'division by zero';
            return 0;
        }
        return $a / $b;
    }
    if ($op === 'mod') {
        if ($b == 0.0) {
            $error = 'modulo by zero';
            return 0;
        }
        return fmod($a, $b);
    }
    if ($op === 'pow') {
        return pow($a, $b);
    }
    return $a + $b;
}

function selected($actual, $expected) {
    return $actual === $expected ? ' selected' : '';
}

$a = num_param('a', 42);
$b = num_param('b', 2);
$op = op_param();
$error = '';
$result = calculate($a, $b, $op, $error);
$method = isset($_SERVER['REQUEST_METHOD']) ? $_SERVER['REQUEST_METHOD'] : 'GET';

header('Content-Type: text/html; charset=utf-8');
?>
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <title>PHP CGI calculator</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 32px; line-height: 1.45; }
    form { display: flex; flex-wrap: wrap; gap: 10px; align-items: end; margin-bottom: 22px; }
    label { display: grid; gap: 4px; font-size: 14px; }
    input, select, button { font: inherit; padding: 8px 10px; }
    table { border-collapse: collapse; min-width: 520px; }
    th, td { border: 1px solid #ccc; padding: 8px 10px; text-align: left; }
    .result { font-size: 28px; font-weight: 700; margin: 12px 0; }
    .error { color: #b00020; font-weight: 700; }
    code { background: #f2f2f2; padding: 2px 4px; }
  </style>
</head>
<body>
  <h1>PHP CGI calculator</h1>

  <form method="post" action="/calculator.php">
    <label>A
      <input type="number" step="any" name="a" value="<?php echo h($a); ?>">
    </label>
    <label>Operation
      <select name="op">
        <option value="add"<?php echo selected($op, 'add'); ?>>+</option>
        <option value="sub"<?php echo selected($op, 'sub'); ?>>-</option>
        <option value="mul"<?php echo selected($op, 'mul'); ?>>*</option>
        <option value="div"<?php echo selected($op, 'div'); ?>>/</option>
        <option value="mod"<?php echo selected($op, 'mod'); ?>>%</option>
        <option value="pow"<?php echo selected($op, 'pow'); ?>>^</option>
      </select>
    </label>
    <label>B
      <input type="number" step="any" name="b" value="<?php echo h($b); ?>">
    </label>
    <button type="submit">Calculate</button>
  </form>

  <?php if ($error !== ''): ?>
    <p class="error"><?php echo h($error); ?></p>
  <?php else: ?>
    <p class="result"><?php echo h($a); ?> <?php echo h(op_label($op)); ?> <?php echo h($b); ?> = <?php echo h($result); ?></p>
  <?php endif; ?>

  <table>
    <tbody>
      <tr><th>script</th><td>calculator.php</td></tr>
      <tr><th>method</th><td><?php echo h($method); ?></td></tr>
      <tr><th>interpreter</th><td><code>/usr/bin/php-cgi</code></td></tr>
      <tr><th>a</th><td><?php echo h($a); ?></td></tr>
      <tr><th>operation</th><td><?php echo h($op); ?></td></tr>
      <tr><th>b</th><td><?php echo h($b); ?></td></tr>
      <tr><th>result</th><td><?php echo $error === '' ? h($result) : h($error); ?></td></tr>
    </tbody>
  </table>

  <p><a href="/">Back to index</a></p>
</body>
</html>
