#include "shell.h"   /* Tokenizer Functions */
/* Duplicates a string */
char *dup_n(const char *src, int n) {
  char *out = (char *)malloc((size_t)n + 1);
  if (out == NULL) {
    perror("malloc() failed");
    exit(1);
  }
  memcpy(out, src, (size_t)n);
  out[n] = '\0';
  return out;
}
/* Pushes a token to the token list */
int push_token(Token **tokens, int *count, int *cap, TokenType type,
               const char *start, int n) {
  if (*count >= *cap) {
    *cap *= 2;
    Token *tmp = (Token *)realloc(*tokens, (size_t)(*cap) * sizeof(Token));
    if (tmp == NULL) {
      perror("realloc() failed");
      return -1;
    }
    *tokens = tmp;
  }

  (*tokens)[*count].type = type;
  (*tokens)[*count].text = dup_n(start, n);
  (*count)++;
  return 0;
}


/* Tokenize the input into a list of tokens */
int tokenize_input(const char *input, Token **out_tokens, int *out_count) {
  int i = 0;
  int cap = 16;
  int count = 0;
  Token *tokens = (Token *)malloc((size_t)cap * sizeof(Token));

  if (tokens == NULL) {
    perror("malloc() failed");
    return -1;
  }

  while (input[i] != '\0') {
    if (isspace((unsigned char)input[i])) {
      i++;
    } else if (input[i] == '#') {
      /* Comment: ignore the rest of the line */
      break;
    } else if (input[i] == ';') {
      if (push_token(&tokens, &count, &cap, TOK_SEMI, input + i, 1) < 0) {
        free_tokens(tokens, count);
        return -1;
      }
      i++;
    } else if (input[i] == '&' && input[i + 1] == '&') {
      if (push_token(&tokens, &count, &cap, TOK_AND, input + i, 2) < 0) {
        free_tokens(tokens, count);
        return -1;
      }
      i += 2;
    } else if (input[i] == '|' && input[i + 1] == '|') {
      if (push_token(&tokens, &count, &cap, TOK_OR, input + i, 2) < 0) {
        free_tokens(tokens, count);
        return -1;
      }
      i += 2;
    } else if (input[i] == '|') {
      if (push_token(&tokens, &count, &cap, TOK_PIPE, input + i, 1) < 0) {
        free_tokens(tokens, count);
        return -1;
      }
      i++;
    } else if (input[i] == '>') {
      if (input[i + 1] == '>') {
        if (push_token(&tokens, &count, &cap, TOK_REDIR_APPEND, input + i, 2) <
            0) {
          free_tokens(tokens, count);
          return -1;
        }
        i += 2;
      } else {
        if (push_token(&tokens, &count, &cap, TOK_REDIR_OUT, input + i, 1) <
            0) {
          free_tokens(tokens, count);
          return -1;
        }
        i++;
      }
    } else if (input[i] == '<') {
      if (push_token(&tokens, &count, &cap, TOK_REDIR_IN, input + i, 1) < 0) {
        free_tokens(tokens, count);
        return -1;
      }
      i++;
    } else if (input[i] == '&') {
      if (push_token(&tokens, &count, &cap, TOK_BG, input + i, 1) < 0) {
        free_tokens(tokens, count);
        return -1;
      }
      i++;
    } else {
      /* Handle WORDs with quotes */
      int start = i;
      int in_single = 0, in_double = 0;
      /* Calculate string length handling quotes */
      while (input[i] != '\0') {
        if (!in_single && !in_double &&
            (isspace((unsigned char)input[i]) || input[i] == ';' ||
             input[i] == '|' || input[i] == '&' || input[i] == '<' ||
             input[i] == '>' || input[i] == '#')) {
          break;
        }
        if (input[i] == '\'' && !in_double)
          in_single = !in_single;
        else if (input[i] == '"' && !in_single)
          in_double = !in_double;
        i++;
      }

      char *clean = (char *)malloc((size_t)(i - start + 1));
      if (clean == NULL) {
        perror("malloc() failed");
        free_tokens(tokens, count);
        return -1;
      }
      int c_idx = 0;
      int j;
      in_single = 0;
      in_double = 0;
      for (j = start; j < i; j++) {
        if (input[j] == '\'' && !in_double) {
          in_single = !in_single;
        } else if (input[j] == '"' && !in_single) {
          in_double = !in_double;
        } else {
          clean[c_idx++] = input[j];
        }
      }
      clean[c_idx] = '\0';
      if (push_token(&tokens, &count, &cap, TOK_WORD, clean, c_idx) < 0) {
        free(clean);
        free_tokens(tokens, count);
        return -1;
      }
      free(clean);
    }
  }

  if (push_token(&tokens, &count, &cap, TOK_EOF, "<eof>", 5) < 0) {
    free_tokens(tokens, count);
    return -1;
  }
  *out_tokens = tokens;
  *out_count = count;
  return 0;
}


/* Free mem. allocated to tokens */
void free_tokens(Token *tokens, int count) {
  int i;
  for (i = 0; i < count; i++) {
    free(tokens[i].text);
  }
  free(tokens);
}

