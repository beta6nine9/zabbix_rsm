<?php

namespace Modules\RsmProvisioningApi;

use Exception;

class RsmException extends Exception
{
	private int     $resultCode;
	private string  $title;
	private ?string $description;

	public function __construct(int $resultCode, string $title, ?string $description = null)
	{
		parent::__construct($title, 0, NULL);

		$this->resultCode    = $resultCode;
		$this->title         = $title;
		$this->description   = $description;
	}

	public function getResultCode(): int
	{
		return $this->resultCode;
	}

	public function getTitle(): string
	{
		return $this->title;
	}

	public function getDescription(): ?string
	{
		return $this->description;
	}
}
