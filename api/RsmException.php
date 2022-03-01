<?php

class RsmException extends Exception
{
	private int     $resultCode;
	private string  $title;
	private ?string $description;
	private ?array  $details;
	private ?array  $updatedObject;

	public function __construct(int $resultCode, string $title, ?string $description = null, ?array $details = null, ?array $updatedObject = null)
	{
		parent::__construct($title, 0, NULL);

		$this->resultCode    = $resultCode;
		$this->title         = $title;
		$this->description   = $description;
		$this->details       = $details;
		$this->updatedObject = $updatedObject;
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

	public function getDetails(): ?array
	{
		return $this->details;
	}

	public function getUpdatedObject(): ?array
	{
		return $this->updatedObject;
	}
}
