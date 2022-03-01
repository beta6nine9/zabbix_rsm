<?php

class Database
{
	private $handle;

	public function __construct(array $config)
	{
		$dsn = $this->getConnectionDsn($config);
		$options = $this->getConnectionOptions($config);

		$this->handle = new PDO($dsn, $config['user'] , $config['password'], $options);

		$this->handle->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
		$this->handle->setAttribute(PDO::ATTR_DEFAULT_FETCH_MODE, PDO::FETCH_NUM);
		$this->handle->setAttribute(PDO::ATTR_EMULATE_PREPARES, false);
		$this->handle->setAttribute(PDO::MYSQL_ATTR_USE_BUFFERED_QUERY, false);
	}

	public function __destruct()
	{
		$this->handle = null;
	}

	public static function beginTransaction(): void
	{
		$this->handle->beginTransaction();
	}

	public static function rollBack(): void
	{
		$this->handle->rollBack();
	}

	public static function commit(): void
	{
		$this->handle->commit();
	}

	public function select(string $query, ?array $params = null): array
	{
		$sth = $this->handle->prepare($query);
		$sth->execute($params);
		return $sth->fetchAll();
	}

	public function selectRow(string $query, ?array $params = null): array
	{
		$rows = $this->select($query, $params);
		return $rows[0];
	}

	public function selectValue(string $query, ?array $params = null)
	{
		$row = $this->selectRow($query, $params);
		return $row[0];
	}

	public function execute(string $query, ?array $params = null): int
	{
		$sth = self::$dbh->prepare($sql);
		$sth->execute($input_parameters);
		return $sth->rowCount();
	}

	private function getConnectionDsn(array $config): string
	{
		$dsn = 'mysql:';

		if (!is_null($config['host']))
		{
			$dsn .= 'host=' . $config['host'] . ';';
		}
		if (!is_null($config['port']))
		{
			$dsn .= 'port=' . $config['port'] . ';';
		}
		if (!is_null($config['database']))
		{
			$dsn .= 'dbname=' . $config['database'] . ';';
		}

		return $dsn;
	}

	private function getConnectionOptions(array $config): array
	{
		$options = [];

		if (!is_null($config["ssl_key"]))
		{
			$options[PDO::MYSQL_ATTR_SSL_KEY] = $config["ssl_key"];
		}
		if (!is_null($config["ssl_cert"]))
		{
			$options[PDO::MYSQL_ATTR_SSL_CERT] = $config["ssl_cert"];
		}
		if (!is_null($config["ssl_ca"]))
		{
			$options[PDO::MYSQL_ATTR_SSL_CA] = $config["ssl_ca"];
		}
		if (!is_null($config["ssl_capath"]))
		{
			$options[PDO::MYSQL_ATTR_SSL_CAPATH] = $config["ssl_capath"];
		}
		if (!is_null($config["ssl_cipher"]))
		{
			$options[PDO::MYSQL_ATTR_SSL_CIPHER] = $config["ssl_cipher"];
		}

		return $options;
	}
}
