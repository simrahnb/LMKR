--
-- PostgreSQL database dump
--

\restrict g7axPxsyieNl3WA6GSDvmKV5kaGLOh8fQk86vzzgus0adtLb4fv7zBMGMGXhnb8

-- Dumped from database version 16.10 (Homebrew)
-- Dumped by pg_dump version 16.10 (Homebrew)

SET statement_timeout = 0;
SET lock_timeout = 0;
SET idle_in_transaction_session_timeout = 0;
SET client_encoding = 'UTF8';
SET standard_conforming_strings = on;
SELECT pg_catalog.set_config('search_path', '', false);
SET check_function_bodies = false;
SET xmloption = content;
SET client_min_messages = warning;
SET row_security = off;

SET default_tablespace = '';

SET default_table_access_method = heap;

--
-- Name: binary_headers; Type: TABLE; Schema: public; Owner: simrah
--

CREATE TABLE public.binary_headers (
    file_id bigint NOT NULL,
    sample_interval_us integer,
    samples_per_trace integer,
    data_format_code integer,
    raw_json jsonb
);


ALTER TABLE public.binary_headers OWNER TO simrah;

--
-- Name: files; Type: TABLE; Schema: public; Owner: simrah
--

CREATE TABLE public.files (
    id bigint NOT NULL,
    file_path text NOT NULL,
    text_header text,
    created_at timestamp with time zone DEFAULT now(),
    geom_placeholder text
);


ALTER TABLE public.files OWNER TO simrah;

--
-- Name: files_id_seq; Type: SEQUENCE; Schema: public; Owner: simrah
--

CREATE SEQUENCE public.files_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.files_id_seq OWNER TO simrah;

--
-- Name: files_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: simrah
--

ALTER SEQUENCE public.files_id_seq OWNED BY public.files.id;


--
-- Name: trace_headers; Type: TABLE; Schema: public; Owner: simrah
--

CREATE TABLE public.trace_headers (
    id bigint NOT NULL,
    file_id bigint,
    trace_seq integer,
    sample_interval_us integer,
    samples_in_trace integer,
    src_x integer,
    src_y integer,
    rcv_x integer,
    rcv_y integer,
    coord_scalar integer,
    units_code integer,
    raw_json jsonb
);


ALTER TABLE public.trace_headers OWNER TO simrah;

--
-- Name: trace_headers_id_seq; Type: SEQUENCE; Schema: public; Owner: simrah
--

CREATE SEQUENCE public.trace_headers_id_seq
    START WITH 1
    INCREMENT BY 1
    NO MINVALUE
    NO MAXVALUE
    CACHE 1;


ALTER SEQUENCE public.trace_headers_id_seq OWNER TO simrah;

--
-- Name: trace_headers_id_seq; Type: SEQUENCE OWNED BY; Schema: public; Owner: simrah
--

ALTER SEQUENCE public.trace_headers_id_seq OWNED BY public.trace_headers.id;


--
-- Name: files id; Type: DEFAULT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.files ALTER COLUMN id SET DEFAULT nextval('public.files_id_seq'::regclass);


--
-- Name: trace_headers id; Type: DEFAULT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.trace_headers ALTER COLUMN id SET DEFAULT nextval('public.trace_headers_id_seq'::regclass);


--
-- Name: binary_headers binary_headers_pkey; Type: CONSTRAINT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.binary_headers
    ADD CONSTRAINT binary_headers_pkey PRIMARY KEY (file_id);


--
-- Name: files files_file_path_key; Type: CONSTRAINT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.files
    ADD CONSTRAINT files_file_path_key UNIQUE (file_path);


--
-- Name: files files_pkey; Type: CONSTRAINT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.files
    ADD CONSTRAINT files_pkey PRIMARY KEY (id);


--
-- Name: trace_headers trace_headers_pkey; Type: CONSTRAINT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.trace_headers
    ADD CONSTRAINT trace_headers_pkey PRIMARY KEY (id);


--
-- Name: trace_headers_file_idx; Type: INDEX; Schema: public; Owner: simrah
--

CREATE INDEX trace_headers_file_idx ON public.trace_headers USING btree (file_id);


--
-- Name: trace_headers_file_seq_idx; Type: INDEX; Schema: public; Owner: simrah
--

CREATE INDEX trace_headers_file_seq_idx ON public.trace_headers USING btree (file_id, trace_seq);


--
-- Name: binary_headers binary_headers_file_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.binary_headers
    ADD CONSTRAINT binary_headers_file_id_fkey FOREIGN KEY (file_id) REFERENCES public.files(id) ON DELETE CASCADE;


--
-- Name: trace_headers trace_headers_file_id_fkey; Type: FK CONSTRAINT; Schema: public; Owner: simrah
--

ALTER TABLE ONLY public.trace_headers
    ADD CONSTRAINT trace_headers_file_id_fkey FOREIGN KEY (file_id) REFERENCES public.files(id) ON DELETE CASCADE;


--
-- PostgreSQL database dump complete
--

\unrestrict g7axPxsyieNl3WA6GSDvmKV5kaGLOh8fQk86vzzgus0adtLb4fv7zBMGMGXhnb8

