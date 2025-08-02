import socket
from time import sleep
import os
import json
import logging
from dotenv import load_dotenv
from Neo4jClient import Neo4jClient  # Ensure this is the correct import path for your Neo4j client
from langchain.chains.llm import LLMChain
from langchain_core.prompts import PromptTemplate
from langchain_community.llms import Ollama  # or replace with ChatOpenAI if needed

logging.basicConfig(level=logging.INFO, format="[%(levelname)s] %(message)s")

class LangChainCypherAgent:
    def __init__(self, api_key=None):
        self.HOST = "127.0.0.1"
        self.PORT = 7777
        self.GRAPH_ID = "14"
        self.LINE_END = b"\r\n"
        self.CYPHER = b"cypher"

        self.llm = Ollama(model="llama3")  # Replace with ChatOpenAI() if needed
        self.client = Neo4jClient("bolt://localhost:7687", "neo4j", "")


        self.template = """
MATCH (n:Label) RETURN n.name
MATCH (n:Label) RETURN count(n)
MATCH (n:Label) RETURN n.${prop}
MATCH (n:Label {key: 'value', key2: 'value2'}) RETURN n
MATCH (n:Label {key: 'value', key2: 'value2'}) RETURN n.${prop}
MATCH (n:Label {key: 'value', key2: 'value2'}) RETURN n.name, n.${prop}
MATCH (n:Label) WHERE n.${prop1} = '${value1}' AND n.${prop2} = '${value2}' RETURN n.name
MATCH (n:Label {key: 'value', key2: 'value2'}) WHERE n.${prop1} = '${value1}' AND n.${prop2} = '${value2}' RETURN n.name
MATCH (n:Label) RETURN n.${prop}
MATCH (n:Label) WHERE n.${prop1} = '${value1}' OR n.${prop2} = '${value2}' RETURN n.name
MATCH (n:Label {key: 'value', key2: 'value2'}) WHERE n.${prop1} = '${value1}' OR n.${prop2} = '${value2}' RETURN n.name
MATCH (n:Label) RETURN ${agg_clause}
MATCH (n:Label) RETURN n.${key} AS key, count(n) AS num
MATCH (n:Label) RETURN n.${key} AS key, avg(n.${key}) AS avg
MATCH (n:Label)-[r0]->(m0:Label) RETURN count(m0)
MATCH (n:Label)-[r0]->(m0:Label) RETURN n, count(m0) AS num
MATCH (n:Label)-[r0]->(m0:Label) WHERE n.${prop1} = '${value1}' AND m0.${prop2} = '${value2}' RETURN n, m0
MATCH (n:Label)-[r0]->(m0:Label {key: 'value', key2: 'value2'}) RETURN n, m0
MATCH (n:Label)-[r0]->(m0:Label)-[r1]->(m1:Label) WHERE n.${prop1} = '${value1}' AND m0.${prop2} = '${value2}' AND m1.${prop3} = '${value3}' RETURN n, m0, m1
        """

        self.schema = """{
"name": "terrorist_attack",
"entities": [
  {
    "label": "TerroristAttack",
    "properties": {
      "number_of_injuries": "int",
      "number_of_deaths": "int",
      "date": "date",
      "locations": "list[str]"
    }
  },
  {
    "label": "Terrorist",
    "properties": {
      "country_of_citizenship": "list[str]",
      "place_of_birth": "str",
      "gender": "str",
      "date_of_birth": "date"
    }
  },
  {
    "label": "Target",
    "properties": {}
  },
  {
    "label": "Country",
    "properties": {}
  },
  {
    "label": "Weapon",
    "properties": {}
  }
],
"relations": [
  {
    "label": "perpetratedBy",
    "subj_label": "TerroristAttack",
    "obj_label": "Terrorist"
  },
  {
    "label": "occursIn",
    "subj_label": "TerroristAttack",
    "obj_label": "Country"
  },
  {
    "label": "employs",
    "subj_label": "TerroristAttack",
    "obj_label": "Weapon"
  },
  {
    "label": "targets",
    "subj_label": "TerroristAttack",
    "obj_label": "Target"
  }
]
}"""
        self.subquestion_prompt = PromptTemplate(
            input_variables=["question", "schema", "template"],
            template="""
        You are an assistant generating Cypher queries to answer a complex question.

        Given the graph schema:
        {schema}

        And the Cypher query syntax template:
        {template}

        Break down the original question into the minimum number of atomic sub-questions that can be directly answered with simple Cypher queries.

        Return the output in the following JSON format:

        [
          {{
            "subquestion": "<sub-question>",
            "cypher": "<cypher query>"
          }},
          ...
        ]

        Only output a valid JSON array. Do not add any extra text.
        Question: {question}
        """
        )

        self.subquestion_chain = LLMChain(llm=self.llm, prompt=self.subquestion_prompt)

        # NEW: Cypher generation prompt that includes previous result context
        self.query_prompt = PromptTemplate(
            input_variables=["schema", "question", "history"],
            template="""
You are a expert Cypher query assistant helping to generate valid Cypher queries to answer questions.

Using the below information, generate a Cypher query to answer the question. 

Original user question:
{question}

Graph schema, the labels and property keys are case sensitive, so use the exact label names as defined in the schema.:
{schema}


Respond with a valid **single-line** Cypher query **only**. Do not add explanations or any text before/after the query.
        """
        )

        self.query_chain = LLMChain(llm=self.llm, prompt=self.query_prompt)

    def split_into_subquestions(self, question: str) -> list[dict]:
        response = self.subquestion_chain.run(
            question=question.strip(),
            template=self.template,
            schema=self.schema.strip()
        )
        try:
            print(response)
            subq_pairs = json.loads(response)
            return subq_pairs
        except json.JSONDecodeError:
            logging.error("Failed to parse subquestions and cypher queries from LLM output")
            return []

    def generate_cypher(self, question: str, context: str = "", history: str = "") -> str:

        return self.query_chain.run(
            schema=self.schema.strip(),
            question=question.strip(),
            template=self.template.strip(),
            history=history.strip()
        ).strip()

    def send_cypher_query(self, query: str) -> str:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
            sock.connect((self.HOST, self.PORT))
            sock.sendall(self.CYPHER + self.LINE_END)
            sleep(0.1)
            sock.recv(1024)
            sock.sendall(self.GRAPH_ID.encode() + self.LINE_END)
            sock.recv(1024)
            sock.sendall(query.encode() + self.LINE_END)

            response = b""
            while True:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
                if b"done\r" in response:
                    break
        return response

    def summarize_answer(self, all_answers: list[str], original_question: str) -> str:
        # Combine all sub-question answers and generate a human-friendly summary
        combined_context = "\n".join(all_answers)

        summary_prompt = PromptTemplate(
            input_variables=["original_question", "combined_context"],
            template="""
    You are a helpful assistant summarizing information from a graph database.

    Original question: {original_question}

    Here are the sub-questions, their Cypher queries, and raw answers:
    {combined_context}

    Now, based on this information, provide a clear and concise final answer in simple English that a non-technical user can understand. Do not mention Cypher or sub-questions. Just provide the summarized answer to the original question.
    """
        )
        summary_chain = LLMChain(llm=self.llm, prompt=summary_prompt)
        final_answer = summary_chain.run(original_question=original_question, combined_context=combined_context)
        return final_answer.strip()

    def validate_cypher_syntax_with_llm(self, cypher: str) -> bool:
        """
        Use the LLM to validate if the Cypher query is syntactically correct.
        """
        syntax_prompt = PromptTemplate(
            input_variables=["query"],
            template="""
    You are an expert in Cypher query syntax.

    Analyze the following Cypher query and determine whether it is syntactically correct according to Neo4j Cypher standards.

    Query:
    {query}

    Respond with exactly one word: "yes" if the syntax is valid, otherwise "no".
    """
        )
        chain = LLMChain(llm=self.llm, prompt=syntax_prompt)
        response = chain.run(query=cypher).strip().lower()
        return "yes" in response

    def validate_cypher_schema_with_llm(self, cypher: str) -> bool:
        schema_validation_prompt = PromptTemplate(
            input_variables=["schema", "query"],
            template="""
You are an expert in Cypher and Neo4j. Your task is to validate whether the following Cypher query respects the given schema.

Schema:
{schema}

Cypher query:
{query}

Does the query only use labels, relationship types, and properties as defined in the schema? Are all relationship directions and label pairings correct?

Reply only with: "yes" or "no".
"""
        )
        chain = LLMChain(llm=self.llm, prompt=schema_validation_prompt)
        result = chain.run(schema=self.schema.strip(), query=cypher).strip().lower()
        return "yes" in result

    def validate_result_with_llm(self, question: str, cypher: str, result: str) -> bool:
        """
        Ask the LLM whether the result is valid and relevant to the question.
        """
        validation_prompt = PromptTemplate(
            input_variables=["question", "cypher", "result"],
            template="""
You are an expert in validating Cypher query results from a graph database.

Original question:
{question}

Cypher query used:
{cypher}

Result returned (JSON or text):
{result}

Based on the result and the question, is this result relevant, complete, and correct?

Answer with a single word: "yes" or "no". Do not add anything else.
"""
        )
        chain = LLMChain(llm=self.llm, prompt=validation_prompt)
        response = chain.run(question=question, cypher=cypher, result=result).strip().lower()
        return "yes" in response

    def answer_question(self, question: str, max_attempts: int = 5) -> str:
        context = ""
        history_list = []

        for attempt in range(1, max_attempts + 1):
            logging.info(f"Attempt {attempt} to generate and run Cypher query")

            # Create stringified history for this attempt
            history_str = "\n---\n".join([f"Query: {h['cypher']}\nResult: {h['result']}" for h in history_list])

            cypher = self.generate_cypher(question, context=context, history=history_str)
            logging.info(f"Generated Cypher query:\n{cypher}")
            # 🔍 Validate syntax
            # if not self.validate_cypher_syntax_with_llm(cypher):
            #     logging.warning("Cypher query failed syntax validation.")
            #     continue
            #
            # # 🔍 Validate schema
            # if not self.validate_cypher_schema_with_llm(cypher):
            #     logging.warning("Cypher query failed schema validation.")
            #     continue

            try:
                raw_result = self.client.send_cypher_query(cypher)

                # try:
                #     result_data = json.loads(raw_result)
                # except json.JSONDecodeError:
                #     result_data = raw_result.decode(errors="ignore")
                print (f"Raw result: {raw_result}")

                short_result = json.dumps(raw_result)[:3000] if isinstance(raw_result, (dict, list)) else str(
                    raw_result)[:3000]

                # Add to history
                history_list.append({"cypher": cypher, "result": short_result})

                # Use LLM to validate
                is_valid = self.validate_result_with_llm(question, cypher, short_result)

                if is_valid:
                    logging.info("LLM validated the result as correct")
                    context = short_result
                    break
                else:
                    logging.warning("LLM rejected the result, retrying...")
                    context = f"Most recent rejected result: {short_result}"

            except Exception as e:
                err_msg = f"Error running query: {str(e)}"
                logging.warning(err_msg)
                history_list.append({"cypher": cypher, "result": err_msg})
                context = err_msg

        # Final summarization
        summary_prompt = PromptTemplate(
            input_variables=["original_question", "combined_context"],
            template="""
You are a helpful assistant summarizing information from a graph database.

Original question: {original_question}

Here is the raw data returned from the Cypher query:
{combined_context}

Based on this data, provide a clear and concise answer to the original question in simple English.
"""
        )
        summary_chain = LLMChain(llm=self.llm, prompt=summary_prompt)
        final_answer = summary_chain.run(original_question=question, combined_context=context)
        return final_answer.strip()


if __name__ == "__main__":
    load_dotenv()
    agent = LangChainCypherAgent(api_key=os.getenv("OPENAI_API_KEY"))
    # question = input("Ask a graph question: ")
    question = "What are the names and dates of terrorist attacks that targeted both Leopold Cafe and Nariman House?"
    answer = agent.answer_question(question)
    print(answer)
    # questions = [
    #     "Are André Eminger and Sante Geronimo Caserio of the same gender?",
    #     "What are the names and injury counts of terrorist attacks that targeted the same target as the 1993 World Trade Center bombing?",
    #     "What are the names and dates of terrorist attacks that targeted both Leopold Cafe and Nariman House?",
    #     "What is the name of the terrorist attack in Denmark with the fewest deaths?"
    # ]
    # answers = [
    #
    # ]
    # load_dotenv()
    # agent = LangChainCypherAgent(api_key=os.getenv("OPENAI_API_KEY"))
    # # question = input("Ask a graph question: ")
    #
    # for question in questions:
    #     answer = agent.answer_question(question)
    #     answers.append(answer)
    #
    # ## print questions and asnwers
    #
    # for i in range (len(questions)):
    #     print( "Question: "+ questions[i] + " Answer:" + answers[i])
