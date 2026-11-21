CREATE DATABASE IF NOT EXISTS coworking CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
USE coworking;

CREATE TABLE IF NOT EXISTS members (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(120) NOT NULL,
    email VARCHAR(254) NOT NULL UNIQUE,
    phone VARCHAR(30) NOT NULL,
    tier ENUM('Basic','Silver','Gold') NOT NULL DEFAULT 'Basic',
    password_salt CHAR(32) NOT NULL,
    password_hash CHAR(64) NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS workspaces (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(120) NOT NULL UNIQUE,
    type ENUM('Desk','Private Office','Meeting Room') NOT NULL,
    capacity INT UNSIGNED NOT NULL,
    hourly_rate_cents BIGINT UNSIGNED NOT NULL,
    status ENUM('Active','Inactive') NOT NULL DEFAULT 'Active',
    CONSTRAINT chk_capacity CHECK (capacity > 0),
    CONSTRAINT chk_rate CHECK (hourly_rate_cents > 0)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS reservations (
    id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
    member_id BIGINT UNSIGNED NOT NULL,
    workspace_id BIGINT UNSIGNED NOT NULL,
    start_at DATETIME NOT NULL,
    end_at DATETIME NOT NULL,
    status ENUM('Booked','Checked In','Completed','Cancelled') NOT NULL DEFAULT 'Booked',
    base_cents BIGINT UNSIGNED NOT NULL,
    surcharge_cents BIGINT UNSIGNED NOT NULL,
    discount_cents BIGINT UNSIGNED NOT NULL,
    total_cents BIGINT UNSIGNED NOT NULL,
    created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_member FOREIGN KEY (member_id) REFERENCES members(id) ON DELETE RESTRICT,
    CONSTRAINT fk_workspace FOREIGN KEY (workspace_id) REFERENCES workspaces(id) ON DELETE RESTRICT,
    CONSTRAINT chk_time CHECK (end_at > start_at),
    INDEX idx_conflict (workspace_id, status, start_at, end_at),
    INDEX idx_member (member_id, start_at),
    INDEX idx_reports (status, start_at)
) ENGINE=InnoDB;
